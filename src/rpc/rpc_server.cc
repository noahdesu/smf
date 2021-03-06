// Copyright (c) 2016 Alexander Gallego. All rights reserved.
//
#include "rpc/rpc_server.h"

// seastar
#include <core/execution_stage.hh>
#include <core/metrics.hh>
#include <core/prometheus.hh>

#include "histogram/histogram_seastar_utils.h"
#include "platform/log.h"
#include "rpc/rpc_envelope.h"


namespace smf {
namespace stdx = std::experimental;

std::ostream &operator<<(std::ostream &o, const smf::rpc_server &s) {
  o << "rpc_server{args.ip=" << s.args_.ip << ", args.flags=" << s.args_.flags
    << ", args.rpc_port=" << s.args_.rpc_port
    << ", args.http_port=" << s.args_.http_port << ", rpc_routes=" << s.routes_
    << ", limits=" << *s.limits_.get() << "}";
  return o;
}

rpc_server::rpc_server(rpc_server_args args)
  : args_(args)
  , limits_(
      seastar::make_lw_shared<rpc_connection_limits>(args.basic_req_size,
                                                     args.bloat_mult,
                                                     args.memory_avail_per_core,
                                                     args.recv_timeout)) {
  namespace sm = seastar::metrics;
  metrics_.add_group(
    "smf::rpc_server",
    {
      sm::make_derive("active_connections", stats_->active_connections,
                      sm::description("Currently active connections")),
      sm::make_derive("total_connections", stats_->total_connections,
                      sm::description("Counts a total connetions")),
      sm::make_derive("incoming_bytes", stats_->in_bytes,
                      sm::description("Total bytes received of healthy "
                                      "connections - ignores bad connections")),
      sm::make_derive("outgoing_bytes", stats_->out_bytes,
                      sm::description("Total bytes sent to clients")),
      sm::make_derive("bad_requests", stats_->bad_requests,
                      sm::description("Bad requests")),
      sm::make_derive(
        "no_route_requests", stats_->no_route_requests,
        sm::description(
          "Requests made to this sersvice with correct header but no handler")),
      sm::make_derive("completed_requests", stats_->completed_requests,
                      sm::description("Correct round-trip returned responses")),
      sm::make_derive(
        "too_large_requests", stats_->too_large_requests,
        sm::description(
          "Requests made to this server larger than max allowedd (2GB)")),
    });
}

rpc_server::~rpc_server() {}

void rpc_server::start() {
  LOG_INFO("Starging server:{}", *this);
  if (!(args_.flags & rpc_server_flags_disable_http_server)) {
    LOG_INFO("Starting HTTP admin server on background future");
    admin_ = seastar::make_lw_shared<seastar::http_server>("smf admin server");
    LOG_INFO("HTTP server started, adding prometheus routes");
    seastar::prometheus::config conf;
    conf.metric_help = "smf-broker statistics";
    conf.prefix      = "smf";
    // start on background co-routine
    seastar::prometheus::add_prometheus_routes(*admin_, conf).then([
      http_port = args_.http_port, admin = admin_, ip = args_.ip
    ]() {
      return admin
        ->listen(seastar::make_ipv4_address(
          ip.empty() ? seastar::ipv4_addr{http_port} :
                       seastar::ipv4_addr{ip, http_port}))
        .handle_exception([](auto ep) {
          LOG_ERROR("Exception on HTTP Admin: {}", ep);
          return seastar::make_exception_future<>(ep);
        });
    });
  }
  LOG_INFO("Starting RPC Server...");
  seastar::listen_options lo;
  lo.reuse_address = true;
  listener_        = seastar::listen(
    seastar::make_ipv4_address(args_.ip.empty() ?
                                 seastar::ipv4_addr{args_.rpc_port} :
                                 seastar::ipv4_addr{args_.ip, args_.rpc_port}),
    lo);
  seastar::keep_doing([this] {
    return listener_->accept().then([this, stats = stats_, limits = limits_](
      seastar::connected_socket fd, seastar::socket_address addr) mutable {
      auto conn = seastar::make_lw_shared<rpc_server_connection>(
        std::move(fd), limits, addr, stats, ++connection_idx_);

      open_connections_.insert({connection_idx_, conn});

      // DO NOT return the future. Need to execute in parallel
      handle_client_connection(conn);
    });
  }).handle_exception([](auto ep) {
    // Current and future \ref accept() calls will terminate immediately
    // with an error after listener_->abort_accept().
    // prevent future connections
    LOG_WARN("Server stopped accepting connections: `{}`", ep);
  });
}

seastar::future<> rpc_server::stop() {
  LOG_WARN("Stopping rpc server: aborting future accept() calls");
  listener_->abort_accept();

  std::for_each(
    open_connections_.begin(), open_connections_.end(),
    [](auto &client_conn) { client_conn.second->socket.shutdown_input(); });

  return limits_->reply_gate.close().then([admin = admin_ ? admin_ : nullptr] {
    if (!admin) { return seastar::make_ready_future<>(); }
    return admin->stop().handle_exception([](auto ep) {
      LOG_WARN("Error (ignoring...) shutting down HTTP server: {}", ep);
      return seastar::make_ready_future<>();
    });
  });
}

seastar::future<> rpc_server::handle_client_connection(
  seastar::lw_shared_ptr<rpc_server_connection> conn) {
  return seastar::do_until(
    [conn] { return !conn->is_valid(); },
    [this, conn]() mutable {
      return rpc_recv_context::parse(conn.get()).then([
        this, conn, h = hist_
      ](stdx::optional<rpc_recv_context> recv_ctx) mutable {
        if (!recv_ctx) {
          conn->set_error("Could not parse the request");
          return seastar::make_ready_future<>();
        }
        auto metric = h->auto_measure();
        /*return*/ stage_apply_incoming_filters(std::move(recv_ctx.value()))
          .then([this, conn,
                 metric = std::move(metric)](rpc_recv_context ctx) mutable {
            auto payload_size = ctx.payload.size();
            return this->dispatch_rpc(conn, std::move(ctx)).finally([
              this, conn, metric = std::move(metric), payload_size
            ] {
              conn->limits->release_payload_resources(payload_size);
              return conn->ostream.flush().finally([this, conn] {
                if (conn->has_error()) {
                  LOG_ERROR("There was an error with the connection: {}",
                            conn->get_error());
                  conn->stats->bad_requests++;
                  conn->stats->active_connections--;
                  LOG_INFO("Closing connection for client: {}",
                           conn->remote_address);
                  if (open_connections_.find(conn->id)
                      != open_connections_.end()) {
                    open_connections_.erase(conn->id);
                  }
                  return conn->ostream.close();
                } else {
                  conn->stats->completed_requests++;
                  return seastar::make_ready_future<>();
                }
              });
            });  // end finally()
          })     //  parse_rpc_recv_context.then()
          .handle_exception([this, conn](auto eptr) {
            LOG_ERROR("Caught exception dispatching rpc: {}", eptr);
            conn->set_error("Exception parsing request ");
            if (open_connections_.find(conn->id) != open_connections_.end()) {
              open_connections_.erase(conn->id);
            }
            return conn->ostream.close().then_wrapped([](auto _) {});
          });
        return seastar::make_ready_future<>();
      });
    });
}

seastar::future<> rpc_server::dispatch_rpc(
  seastar::lw_shared_ptr<rpc_server_connection> conn, rpc_recv_context &&ctx) {
  if (ctx.request_id() == 0) {
    conn->set_error("Missing request_id. Invalid request");
    return seastar::make_ready_future<>();
  }
  if (!routes_.can_handle_request(ctx.request_id())) {
    conn->stats->no_route_requests++;
    conn->set_error("Can't find route for request. Invalid");
    return seastar::make_ready_future<>();
  }
  conn->stats->in_bytes += ctx.header.size() + ctx.payload.size();


  /// the request follow [filters] -> handle -> [filters]
  /// the only way for the handle not to receive the information is if
  /// the filters invalidate the request - they have full mutable access
  /// to it, or they throw an exception if they wish to interrupt the entire
  /// connection
  return seastar::with_gate(
           conn->limits->reply_gate,
           [this, ctx = std::move(ctx), conn]() mutable {
             return routes_.handle(std::move(ctx))
               .then([this](rpc_envelope e) {
                 return stage_apply_outgoing_filters(std::move(e));
               })
               .then([this, conn](rpc_envelope e) {
                 conn->stats->out_bytes += e.letter.size();
                 return conn->serialize_writes.wait(1)
                   .then([conn, ee = std::move(e)]() mutable {
                     return smf::rpc_envelope::send(&conn->ostream,
                                                    std::move(ee));

                   })
                   .finally([conn] { conn->serialize_writes.signal(1); });
               });
           })
    .handle_exception([this, conn](auto ptr) {
      LOG_INFO("Cannot dispatch rpc. Server is shutting down...");
      conn->disable();
      return seastar::make_ready_future<>();
    });
}
static thread_local auto incoming_stage = seastar::make_execution_stage(
  "smf::rpc_server::incoming::filter", &rpc_server::apply_incoming_filters);

static thread_local auto outgoing_stage = seastar::make_execution_stage(
  "smf::rpc_server::outgoing::filter", &rpc_server::apply_outgoing_filters);


seastar::future<rpc_recv_context> rpc_server::apply_incoming_filters(
  rpc_recv_context ctx) {
  return rpc_filter_apply(&in_filters_, std::move(ctx));
}
seastar::future<rpc_envelope> rpc_server::apply_outgoing_filters(
  rpc_envelope e) {
  return rpc_filter_apply(&out_filters_, std::move(e));
}

seastar::future<rpc_recv_context> rpc_server::stage_apply_incoming_filters(
  rpc_recv_context ctx) {
  return incoming_stage(this, std::move(ctx));
}
seastar::future<rpc_envelope> rpc_server::stage_apply_outgoing_filters(
  rpc_envelope e) {
  return outgoing_stage(this, std::move(e));
}
}  // namespace smf
