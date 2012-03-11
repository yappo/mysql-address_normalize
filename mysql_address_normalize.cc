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

extern "C" {

my_bool address_normalize_init(UDF_INIT* initid, UDF_ARGS* args, char* message);
void address_normalize_deinit(UDF_INIT* initid);
char* address_normalize(UDF_INIT* initid, UDF_ARGS* args, char* result, unsigned long* length, char* is_null, char* error);

}


struct write_stream {
  char *data;
  size_t len;
  size_t pool_size;
};


static size_t writedata(void *ptr, size_t size, size_t nmemb, void *stream)
{
  struct write_stream *buf = (write_stream *) stream;
  size_t block, buf_size;

  block    = size * nmemb;
  buf_size = buf->len + block;
  if (buf->pool_size <= buf_size) {
    char *new_buf;
    size_t new_pool_size = buf->pool_size ? buf->pool_size : 256;

    while (new_pool_size <= buf_size)
      new_pool_size += new_pool_size;

    new_buf = (char *) my_realloc(buf->data, new_pool_size, MYF(MY_WME));
    if (!new_buf)
      return 0;

    buf->data                = new_buf;
    buf->data[new_pool_size] = new_pool_size;
  }

  memcpy(buf->data + buf->len, ptr, block);
  buf->len            = buf_size;
  buf->data[buf_size] = '\0';

  return block;
}


my_bool address_normalize_init(UDF_INIT* initid, UDF_ARGS* args, char* message)
{
  CURL *curl;

  if (args->arg_count != 1)
  {
    strncpy(message, "address_normalize: required 1 argument", MYSQL_ERRMSG_SIZE);
    return 1;
  }
  if (args->arg_type[0] != STRING_RESULT)
  {
    strncpy(message, "address_normalize: argument should be a string", MYSQL_ERRMSG_SIZE);
    return 1;
  }

  args->maybe_null[0] = 0;

  curl = curl_easy_init();
  if (!curl)
  {
    strncpy(message, "address_normalize: curl init fail", MYSQL_ERRMSG_SIZE);
    return 1;
  }

  curl_easy_setopt(curl, CURLOPT_UPLOAD, 0);
  curl_easy_setopt(curl, CURLOPT_POST, 0);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writedata);

  initid->ptr        = (char *)(void *)curl;
  initid->const_item = 1;

  return 0;
}


void address_normalize_deinit(UDF_INIT* initid)
{
  curl_easy_cleanup((CURL *)(void *)initid->ptr);
}


using namespace std;
using namespace picojson;
char* address_normalize(UDF_INIT* initid, UDF_ARGS* args, char* result, unsigned long* length, char* is_null, char* error)
{
  CURL *curl;
  CURLcode status;
  char *escaped_addres, *uri, *normalized_address;
  size_t escaped_addres_length, normalized_address_length;
  struct write_stream curl_buf = { NULL, 0, 0 };

  curl = (CURL *) initid->ptr;

  escaped_addres = curl_easy_escape(curl, args->args[0], args->lengths[0]);
  if (!escaped_addres)
    goto error;

  escaped_addres_length = strlen(escaped_addres);
  uri = (char *) malloc(LOCTOUCH_API_LENGTH + strlen(escaped_addres) + 1);
  memcpy(uri, LOCTOUCH_API, LOCTOUCH_API_LENGTH);
  memcpy(uri + LOCTOUCH_API_LENGTH, escaped_addres, escaped_addres_length);
  uri[LOCTOUCH_API_LENGTH + escaped_addres_length] = '\0';

  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &curl_buf);
  curl_easy_setopt(curl, CURLOPT_URL, uri);

  status = curl_easy_perform(curl);
  if (status != CURLE_OK)
  {
    goto error;
  } else
  {
    value v;
    string err;

    parse(v, curl_buf.data, curl_buf.data + curl_buf.len, &err);
    if (err.empty()) {
      if (! v.is<object>())
        goto error;
      object obj  = v.get<object>();
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

  if (normalized_address_length > *length)
  {
    result = (char *) my_malloc(normalized_address_length, MYF(MY_WME));
    if (!result)
      goto error;
  }

  memcpy(result, normalized_address, normalized_address_length);
  *length = normalized_address_length;

  return result;

  error:
    *is_null = 1;
    *error = 1;
    return 0;
}
