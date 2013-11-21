/*
 * Copyright 2013 Formation Data Systems, Inc.
 */
#include <am-engine/am-engine.h>
#include <am-plugin.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

extern "C" {
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
} // extern "C"

#include <iostream>
#include <boost/program_options.hpp>
#include <boost/program_options/parsers.hpp>
#include <shared/fds_types.h>
#include <fds_assert.h>
#include <native_api.h>
#include <am-plugin.h>

namespace fds {

const int    NGINX_ARG_PARAM = 80;
static char  nginx_prefix[NGINX_ARG_PARAM];
static char  nginx_config[NGINX_ARG_PARAM];
static char  nginx_signal[NGINX_ARG_PARAM];

static char const *nginx_start_argv[] =
{
    nullptr,
    "-p", nginx_prefix,
    "-c", nginx_config
};

static char const *nginx_signal_argv[] =
{
    nullptr,
    "-s", nginx_signal
};

AMEngine gl_AMEngine("AM Engine Module");

int
AMEngine::mod_init(SysParams const *const p)
{
    using namespace std;
    namespace po = boost::program_options;

    Module::mod_init(p);
    po::variables_map       vm;
    po::options_description desc("Access Manager Options");

    desc.add_options()
        ("help", "Show this help text")
        ("signal", po::value<std::string>(&eng_signal),
         "Send signal to access manager: stop, quit, reopen, reload");

    po::store(po::command_line_parser(p->p_argc, p->p_argv).
            options(desc).allow_unregistered().run(), vm);
    po::notify(vm);
    if (vm.count("help")) {
        cout << desc << endl;
        return 1;
    }
    if (vm.count("signal")) {
        if ((eng_signal != "stop") && (eng_signal != "quit") &&
            (eng_signal != "reopen") && (eng_signal != "reload")) {
            cout << "Expect --signal stop|quit|reopen|reload" << endl;
            return 1;
        }
    }
    return 0;
}

void
AMEngine::mod_startup()
{
}

// make_nginix_dir
// ---------------
// Make a directory that nginx server requies.
//
static void
make_nginx_dir(char const *const path)
{
    if (mkdir(path, 0755) != 0) {
        if (errno == EACCES) {
            std::cout << "Don't have permission to " << path << std::endl;
            exit(1);
        }
        fds_verify(errno == EEXIST);
    }
}

// run_server
// ----------
//
void
AMEngine::run_server(FDS_NativeAPI *api)
{
    using namespace std;
    const string *fds_root;
    char          path[NGINX_ARG_PARAM];

    eng_api = api;
    fds_root = &mod_params->fds_root;
    if (eng_signal != "") {
        nginx_signal_argv[0] = mod_params->p_argv[0];
        strncpy(nginx_signal, eng_signal.c_str(), NGINX_ARG_PARAM);
        ngx_main(FDS_ARRAY_ELEM(nginx_signal_argv), nginx_signal_argv);
        return;
    }
    strncpy(nginx_config, eng_conf, NGINX_ARG_PARAM);
    snprintf(nginx_prefix, NGINX_ARG_PARAM, "%s", fds_root->c_str());

    // Create all directories if they are not exists.
    umask(0);
    snprintf(path, NGINX_ARG_PARAM, "%s/%s", nginx_prefix, eng_logs);
    ModuleVector::mod_mkdir(path);
    snprintf(path, NGINX_ARG_PARAM, "%s/%s", nginx_prefix, eng_etc);
    ModuleVector::mod_mkdir(path);

    nginx_start_argv[0] = mod_params->p_argv[0];
    ngx_main(FDS_ARRAY_ELEM(nginx_start_argv), nginx_start_argv);
}

void
AMEngine::mod_shutdown()
{
}

// ---------------------------------------------------------------------------
// Generic request/response protocol through NGINX module.
// ---------------------------------------------------------------------------
AME_Request::AME_Request(HttpRequest &req)
    : fdsio::Request(false), ame_req(req)
{
  if (ame_req.getNginxReq()->request_body &&
      ame_req.getNginxReq()->request_body->bufs) {
    post_buf_itr = ame_req.getNginxReq()->request_body->bufs;
  } else {
    post_buf_itr = NULL;
  }
}

AME_Request::~AME_Request()
{
}

// ame_reqt_iter_reset
// -------------------
//
void
AME_Request::ame_reqt_iter_reset()
{
}

// ame_reqt_iter_next
// ------------------
//
ame_ret_e
AME_Request::ame_reqt_iter_next()
{
  // todo: rao implement this to the post/put client buffer
    return AME_OK;
}

// ame_reqt_iter_data
// ------------------
//
char const *const
AME_Request::ame_reqt_iter_data(int *len)
{
  if (post_buf_itr == NULL) {
    *len = 0;
    return NULL;
  }

  char const *const data = (char const *const) post_buf_itr->buf->start;
  *len = post_buf_itr->buf->end - post_buf_itr->buf->start;
  post_buf_itr = post_buf_itr->next;

  return data;
}

// ame_get_reqt_hdr_val
// --------------------
//
char const *const
AME_Request::ame_get_reqt_hdr_val(char const *const key)
{
  // todo: rao implement this
    return NULL;
}

// ame_set_resp_keyval
// -------------------
//
ame_ret_e
AME_Request::ame_set_resp_keyval(char const *const k, char const *const v)
{
  // todo: rao implement this
    return AME_OK;
}

// ame_set_std_resp
// ----------------
// Common code path to set standard response to client.
//
ame_ret_e
AME_Request::ame_set_std_resp(int status, int len)
{
    ngx_http_request_t *r;

    r = ame_req.getNginxReq();
    r->headers_out.status           = NGX_HTTP_OK;
    r->headers_out.content_length_n = len;

    return AME_OK;
}

// ame_send_response_hdr
// ---------------------
// Common code path to send the complete response header to client.
//
ame_ret_e
AME_Request::ame_send_response_hdr()
{
    ngx_int_t          rc;
    ngx_http_request_t *r;

    // Any protocol-connector specific response format.
    ame_format_response_hdr();

    // Do actual sending.
    r  = ame_req.getNginxReq();
    rc = ngx_http_send_header(r);
    return AME_OK;
}

// ame_push_resp_data_buf
// ----------------------
//
void *
AME_Request::ame_push_resp_data_buf(int ask, char **buf, int *got)
{
    ngx_buf_t          *b;
    ngx_http_request_t *r;

    r = ame_req.getNginxReq();
    b = (ngx_buf_t *)ngx_calloc_buf(r->pool);
    memset(b, sizeof(ngx_buf_t), 0);

    b->memory        = 1;
    b->last_buf      = 0;
    b->last_in_chain = 0;

    if (ask > 0) {
      b->start = (u_char *)ngx_palloc(r->pool, ask);
      b->end   = b->start + ask;
      b->pos   = b->start;
      b->last  = b->end;
    }
    *buf = (char *)b->pos;
    *got = ask;
    return (void *)b;
}

// ame_send_resp_data
// ------------------
//
ame_ret_e
AME_Request::ame_send_resp_data(void *buf_cookie, int len, fds_bool_t last)
{
    ngx_buf_t          *buf;
    ngx_int_t          rc;
    ngx_chain_t        out;
    ngx_http_request_t *r;

    r   = ame_req.getNginxReq();

    buf = (ngx_buf_t *)buf_cookie;
    buf->end = buf->start + len;
    out.buf  = buf;
    out.next = NULL;

    if (last == true) {
        buf->last_buf      = 1;
        buf->last_in_chain = 1;
    }

    rc = ngx_http_output_filter(r, &out);
    return AME_OK;
}

// ---------------------------------------------------------------------------
// GetObject Connector Adapter
// ---------------------------------------------------------------------------
Conn_GetObject::Conn_GetObject(HttpRequest &req)
    : AME_Request(req)
{
}

Conn_GetObject::~Conn_GetObject()
{
}

// get_callback_fn
// ---------------
//
static FDSN_Status
get_callback_fn(void *req, fds_uint64_t bufsize, const char *buf, void *cb, FDSN_Status status, ErrorDetails *errdetails)
{
    return FDSN_StatusOK;
}

// ame_request_handler
// -------------------
//
void
Conn_GetObject::ame_request_handler()
{
    int           get_len, got_len;
    char          *buf;
    void          *cookie;
    FDS_NativeAPI *api;
    BucketContext *bucket_ctx = NULL;
    std::string    key = get_object_id();
    std::string bucket_id = get_bucket_id();

    // todo: fill bucket context

    get_len = 100;
    cookie  = fdsn_alloc_get_buffer(get_len, &buf, &got_len);

    api = ame_fds_hook();
    // todo: remove comment
//    api->GetObject(bucket_ctx, key, NULL, 0, get_len, buf, get_len,
//                  (void *)this, get_callback_fn, NULL);

    // todo: move this code into callback once cb is implemented
    fdsn_send_get_response(0, get_len);
    fdsn_send_get_buffer(cookie, get_len, true);
}

// fdsn_send_get_response
// ----------------------
//
void
Conn_GetObject::fdsn_send_get_response(int status, int get_len)
{
    ame_set_std_resp(status, get_len);
    ame_send_response_hdr();
}

// ---------------------------------------------------------------------------
// PutObject Connector Adapter
// ---------------------------------------------------------------------------
Conn_PutObject::Conn_PutObject(HttpRequest &req)
    : AME_Request(req)
{
}

Conn_PutObject::~Conn_PutObject()
{
}

// put_callback_fn
// ---------------
//
static int
put_callback_fn(void *req, fds_uint64_t size, char *buf, void *cb, FDSN_Status status, ErrorDetails *errdetails)
{
    return 0;
}

// ame_request_handler
// -------------------
//
void
Conn_PutObject::ame_request_handler()
{
    fds_uint64_t  len;
    const char          *buf;
    FDS_NativeAPI *api;
    std::string   key;
    void *resp_buf;
    char *temp;
    int resp_buf_len = 2;

    buf = ame_reqt_iter_data((int*) &len);
    if (buf == NULL || len == 0) {
      // todo: instead of assert put a log.  Also think about what needes
      // to be returned to server
      fds_assert(!"no body");
      return ;
    }
    api = ame_fds_hook();
    // todo: uncomment
//    api->PutObject(NULL, key, NULL, (void *)this,
//                   buf, len, put_callback_fn, NULL);

    resp_buf  = ame_push_resp_data_buf(resp_buf_len, &temp, &resp_buf_len);
    fdsn_send_put_response(200, resp_buf_len);
    ame_send_resp_data(resp_buf, resp_buf_len, true);
}

// fdsn_send_put_response
// ----------------------
//
void
Conn_PutObject::fdsn_send_put_response(int status, int put_len)
{
    ame_set_std_resp(status, put_len);
    ame_send_response_hdr();
}

} // namespace fds
