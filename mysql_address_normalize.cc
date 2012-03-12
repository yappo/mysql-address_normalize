extern "C" {
#include <my_global.h>
#include <my_sys.h>
#include <mysql.h>

#include <curl/curl.h>
}

#include <string>
#include "picojson/picojson.h"

#define LOCTOUCH_API "https://api.loctouch.com/v1/geo/address_normalize?address="
#define LOCTOUCH_API_LENGTH 58
#define USER_AGENT "mysql_address_normalize/0.01"

extern "C" {

my_bool address_normalize_init(UDF_INIT* initid, UDF_ARGS* args, char* message);
void address_normalize_deinit(UDF_INIT* initid);
char* address_normalize(UDF_INIT* initid, UDF_ARGS* args, char* result, unsigned long* length, char* is_null, char* error);

}


typedef struct context {
  CURL *curl;
  char *uri;
  char *data;
  size_t len;
  size_t pool_size;
} CTX;


void ctx_reset(CTX *ctx)
{
  if (ctx->len > 0)
  {
    ctx->len     = 0;
    ctx->data[0] = '\0';
  }
}


int ctx_realloc(CTX *ctx, size_t size)
{
  char *new_buf;
  size_t new_pool_size = ctx->pool_size ? ctx->pool_size : 256;

  if (ctx->pool_size > size)
    return 1;

  while (new_pool_size <= size)
    new_pool_size += new_pool_size;

  if (ctx->pool_size == 0)
  {
    new_buf = (char *) malloc(new_pool_size);
  } else {
    new_buf = (char *) realloc(ctx->data, new_pool_size);
  }
  if (!new_buf)
    return 0;

  ctx->data      = new_buf;
  ctx->pool_size = new_pool_size;

  return 1;
}


static size_t writedata(void *ptr, size_t size, size_t nmemb, void *stream)
{
  CTX *ctx = (CTX *) stream;
  size_t block, buf_size;
  int ret;

  block    = size * nmemb;
  buf_size = ctx->len + block;

  ret = ctx_realloc(ctx, buf_size);
  if (ret == 0)
    return 0;

  memcpy(ctx->data + ctx->len, ptr, block);
  ctx->len            = buf_size;
  ctx->data[buf_size] = '\0';

  return block;
}


my_bool address_normalize_init(UDF_INIT* initid, UDF_ARGS* args, char* message)
{
  CURL *curl;
  CTX *ctx;

  if (args->arg_count < 1 || args->arg_count > 2)
  {
    strncpy(message, "address_normalize: required one or two argument", MYSQL_ERRMSG_SIZE);
    return 1;
  }
  if (args->arg_type[0] != STRING_RESULT)
  {
    strncpy(message, "address_normalize: argument should be a string", MYSQL_ERRMSG_SIZE);
    return 1;
  }

  args->arg_type[1]   = INT_RESULT;
  args->maybe_null[0] = 0;
  args->maybe_null[1] = 0;

  curl = curl_easy_init();
  if (!curl)
  {
    strncpy(message, "address_normalize: curl init fail", MYSQL_ERRMSG_SIZE);
    return 1;
  }

  curl_easy_setopt(curl, CURLOPT_UPLOAD, 0);
  curl_easy_setopt(curl, CURLOPT_POST, 0);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writedata);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT);

  ctx = (CTX *) calloc(sizeof(CTX), 1);
  if (!ctx)
  {
    strncpy(message, "address_normalize: context allocation error", MYSQL_ERRMSG_SIZE);
    return 1;
  }
  ctx->curl = curl;

  initid->ptr        = (char *)(void *)ctx;
  initid->const_item = 1;

  return 0;
}


void address_normalize_deinit(UDF_INIT* initid)
{
  CTX *ctx = (CTX *)(void *)initid->ptr;
  curl_easy_cleanup(ctx->curl);
  if (ctx->pool_size > 0)
  {
    free(ctx->data);
  }
  free(ctx);
}


using namespace std;
using namespace picojson;
char* address_normalize(UDF_INIT* initid, UDF_ARGS* args, char* result, unsigned long* length, char* is_null, char* error)
{
  CTX *ctx;
  CURLcode status;
  char *escaped_addres, *uri, *normalized_address;
  size_t escaped_addres_length, normalized_address_length;
  long long is_strict_mode;
  int ret;

  ctx = (CTX *) initid->ptr;

  if (args->lengths[0] == 0)
    goto error;

  escaped_addres = curl_easy_escape(ctx->curl, args->args[0], args->lengths[0]);
  if (!escaped_addres)
    goto error;

  escaped_addres_length = strlen(escaped_addres);
  uri = (char *) malloc(LOCTOUCH_API_LENGTH + strlen(escaped_addres) + 1);
  if (!uri)
    goto error;

  memcpy(uri, LOCTOUCH_API, LOCTOUCH_API_LENGTH);
  memcpy(uri + LOCTOUCH_API_LENGTH, escaped_addres, escaped_addres_length);
  uri[LOCTOUCH_API_LENGTH + escaped_addres_length] = '\0';

  curl_easy_setopt(ctx->curl, CURLOPT_WRITEDATA, ctx);
  curl_easy_setopt(ctx->curl, CURLOPT_URL, uri);

  ctx_reset(ctx);
  status = curl_easy_perform(ctx->curl);
  free(uri);

  if (args->arg_count == 2)
  {
    is_strict_mode = *(args->args[1]);
  } else {
    is_strict_mode = 0;
  }

  if (status != CURLE_OK)
  {
    goto error;
  } else {
    value v;
    string err;

    parse(v, ctx->data, ctx->data + ctx->len, &err);
    if (err.empty()) {
      if (! v.is<object>())
        goto error;
      object obj  = v.get<object>();

      if (is_strict_mode == 1)
      {
        // strict mode
        if (! obj["is_error"].is<double>())
          goto error;
	if (obj["is_error"].get<double>() == 1.0)
          goto error;
      }

      if (! obj["result"].is<object>())
        goto error;
      object result = obj["result"].get<object>();
      if (! result["address"].is<string>())
        goto error;
      string address = result["address"].to_str();

      normalized_address        = &(address)[0];
      normalized_address_length = address.size();
    } else {
      goto error;
    }
  }

  // recycle context data
  ctx_reset(ctx);
  ret = ctx_realloc(ctx, normalized_address_length);
  if (ret == 0)
    goto error;

  memcpy(ctx->data, normalized_address, normalized_address_length);
  *length = normalized_address_length;

  return ctx->data;

  error:
    *is_null = 1;
    *error = 1;
    return NULL;
}
