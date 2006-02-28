/* GTK - The GIMP Toolkit
 * eggcupsutils.h: Statemachine implementation of POST and GET 
 * cup calls which can be used to create a non-blocking cups API
 * Copyright (C) 2003, Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "eggcupsutils.h"

#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>


typedef void (*EggCupsRequestStateFunc) (EggCupsRequest *request);

static void _post_send          (EggCupsRequest *request);
static void _post_write_request (EggCupsRequest *request);
static void _post_write_data    (EggCupsRequest *request);
static void _post_check         (EggCupsRequest *request);
static void _post_read_response (EggCupsRequest *request);

static void _get_send           (EggCupsRequest *request);
static void _get_check          (EggCupsRequest *request);
static void _get_read_data      (EggCupsRequest *request);

struct _EggCupsRequest 
{
  EggCupsRequestType type;

  http_t *http;
  http_status_t last_status;
  ipp_t *ipp_request;

  gchar *server;
  gchar *resource;
  gint data_fd;
  gint attempts;

  EggCupsResult *result;

  gint state;

  gint own_http : 1; 
};

struct _EggCupsResult
{
  gchar *error_msg;
  ipp_t *ipp_response;

  gint is_error : 1;
  gint is_ipp_response : 1;
};

#define REQUEST_START 0
#define REQUEST_DONE 500

#define _EGG_CUPS_MAX_ATTEMPTS 10 
#define _EGG_CUPS_MAX_CHUNK_SIZE 8192

/* POST states */
enum 
{
  POST_SEND = REQUEST_START,
  POST_WRITE_REQUEST,
  POST_WRITE_DATA,
  POST_CHECK,
  POST_READ_RESPONSE,
  POST_DONE = REQUEST_DONE
};

EggCupsRequestStateFunc post_states[] = {_post_send,
                                         _post_write_request,
                                         _post_write_data,
                                         _post_check,
                                         _post_read_response};

/* GET states */
enum
{
  GET_SEND = REQUEST_START,
  GET_CHECK,
  GET_READ_DATA,
  GET_DONE = REQUEST_DONE
};

EggCupsRequestStateFunc get_states[] = {_get_send,
                                        _get_check,
                                        _get_read_data};
                                        
static void
egg_cups_result_set_error (EggCupsResult *result, 
                           const char *error_msg)
{
  result->is_ipp_response = FALSE;

  result->is_error = TRUE;
  result->error_msg = g_strdup (error_msg);
}

EggCupsRequest *
egg_cups_request_new (http_t *connection,
                      EggCupsRequestType req_type, 
                      gint operation_id,
                      gint data_fd,
                      const char *server,
                      const char *resource)
{
  EggCupsRequest *request;
  cups_lang_t *language;
  
  request = g_new0 (EggCupsRequest, 1);
  request->result = g_new0 (EggCupsResult, 1);

  request->result->error_msg = NULL;
  request->result->ipp_response = NULL;

  request->result->is_error = FALSE;
  request->result->is_ipp_response = FALSE;

  request->type = req_type;
  request->state = REQUEST_START;

   if (server)
    request->server = g_strdup (server);
  else
    request->server = g_strdup (cupsServer());


  if (resource)
    request->resource = g_strdup (resource);
  else
    request->resource = g_strdup ("/");
 
  if (connection != NULL)
    {
      request->http = connection;
      request->own_http = FALSE;
    }
  else
    {
      request->http = httpConnectEncrypt (request->server, ippPort(), cupsEncryption());
      httpBlocking (request->http, 0);
      request->own_http = TRUE;
    }

  request->last_status = HTTP_CONTINUE;

  request->attempts = 0;
  request->data_fd = data_fd;

  request->ipp_request = ippNew();
  request->ipp_request->request.op.operation_id = operation_id;
  request->ipp_request->request.op.request_id = 1;

  language = cupsLangDefault ();

  egg_cups_request_ipp_add_string (request, IPP_TAG_OPERATION, IPP_TAG_CHARSET,
                                   "attributes-charset", 
                                   NULL, "utf-8");
	
  egg_cups_request_ipp_add_string (request, IPP_TAG_OPERATION, IPP_TAG_LANGUAGE,
                                   "attributes-natural-language", 
                                   NULL, language->language);

  cupsLangFree (language);

  return request;
}

static void
egg_cups_result_free (EggCupsResult *result)
{
  g_free (result->error_msg);

  if (result->ipp_response)
    ippDelete (result->ipp_response);

  g_free (result);
}

void
egg_cups_request_free (EggCupsRequest *request)
{
  if (request->own_http)
    if (request->http)
      httpClose (request->http);
  
  if (request->ipp_request)
    ippDelete (request->ipp_request);

  g_free (request->server);
  g_free (request->resource);

  egg_cups_result_free (request->result);

  g_free (request);
}

gboolean 
egg_cups_request_read_write (EggCupsRequest *request)
{
  if (request->type == EGG_CUPS_POST)
    post_states[request->state](request);
  else if (request->type == EGG_CUPS_GET)
    get_states[request->state](request);

  if (request->state == REQUEST_DONE)
    {
      return TRUE;
    }
  else
    {
      return FALSE;
    }
}

EggCupsResult *
egg_cups_request_get_result (EggCupsRequest *request)
{
  return request->result;
}

void            
egg_cups_request_ipp_add_string (EggCupsRequest *request,
                                 ipp_tag_t group,
                                 ipp_tag_t tag,
                                 const char *name,
                                 const char *charset,
                                 const char *value)
{
  ippAddString (request->ipp_request,
                group,
                tag,
                name,
                charset,
                value);
}

typedef struct
{
  const char	*name;
  ipp_tag_t	value_tag;
} ipp_option_t;

static const ipp_option_t ipp_options[] =
			{
			  { "blackplot",		IPP_TAG_BOOLEAN },
			  { "brightness",		IPP_TAG_INTEGER },
			  { "columns",			IPP_TAG_INTEGER },
			  { "copies",			IPP_TAG_INTEGER },
			  { "finishings",		IPP_TAG_ENUM },
			  { "fitplot",			IPP_TAG_BOOLEAN },
			  { "gamma",			IPP_TAG_INTEGER },
			  { "hue",			IPP_TAG_INTEGER },
			  { "job-k-limit",		IPP_TAG_INTEGER },
			  { "job-page-limit",		IPP_TAG_INTEGER },
			  { "job-priority",		IPP_TAG_INTEGER },
			  { "job-quota-period",		IPP_TAG_INTEGER },
			  { "landscape",		IPP_TAG_BOOLEAN },
			  { "media",			IPP_TAG_KEYWORD },
			  { "mirror",			IPP_TAG_BOOLEAN },
			  { "natural-scaling",		IPP_TAG_INTEGER },
			  { "number-up",		IPP_TAG_INTEGER },
			  { "orientation-requested",	IPP_TAG_ENUM },
			  { "page-bottom",		IPP_TAG_INTEGER },
			  { "page-left",		IPP_TAG_INTEGER },
			  { "page-ranges",		IPP_TAG_RANGE },
			  { "page-right",		IPP_TAG_INTEGER },
			  { "page-top",			IPP_TAG_INTEGER },
			  { "penwidth",			IPP_TAG_INTEGER },
			  { "ppi",			IPP_TAG_INTEGER },
			  { "prettyprint",		IPP_TAG_BOOLEAN },
			  { "printer-resolution",	IPP_TAG_RESOLUTION },
			  { "print-quality",		IPP_TAG_ENUM },
			  { "saturation",		IPP_TAG_INTEGER },
			  { "scaling",			IPP_TAG_INTEGER },
			  { "sides",			IPP_TAG_KEYWORD },
			  { "wrap",			IPP_TAG_BOOLEAN }
			};


static ipp_tag_t
_find_option_tag (const gchar *option)
{
  int lower_bound, upper_bound, num_options;
  int current_option;
  ipp_tag_t result;

  result = IPP_TAG_ZERO;

  lower_bound = 0;
  upper_bound = num_options = (int)(sizeof(ipp_options) / sizeof(ipp_options[0])) - 1;
  
  while (1)
    {
      int match;
      current_option = (int) (((upper_bound - lower_bound) / 2) + lower_bound);

      match = strcasecmp(option, ipp_options[current_option].name);
      if (match == 0)
        {
	  result = ipp_options[current_option].value_tag;
	  return result;
	}
      else if (match < 0)
        {
          upper_bound = current_option - 1;
	}
      else
        {
          lower_bound = current_option + 1;
	}

      if (upper_bound == lower_bound && upper_bound == current_option)
        return result;

      if (upper_bound < 0)
        return result;

      if (lower_bound > num_options)
        return result;

      if (upper_bound < lower_bound)
        return result;
    }
}

void
egg_cups_request_encode_option (EggCupsRequest *request,
                                const gchar *option,
				const gchar *value)
{
  ipp_tag_t option_tag;

  g_assert (option != NULL);
  g_assert (value != NULL);

  option_tag = _find_option_tag (option);

  if (option_tag == IPP_TAG_ZERO)
    {
      option_tag = IPP_TAG_NAME;
      if (strcasecmp (value, "true") == 0 ||
          strcasecmp (value, "false") == 0)
        {
          option_tag = IPP_TAG_BOOLEAN;
        }
    }
        
  switch (option_tag)
    {
      case IPP_TAG_INTEGER:
      case IPP_TAG_ENUM:
        ippAddInteger (request->ipp_request,
                       IPP_TAG_OPERATION,
                       option_tag,
                       option,
                       strtol (value, NULL, 0));
        break;

      case IPP_TAG_BOOLEAN:
        {
          char b;
          b = 0;
          if (!strcasecmp(value, "true") ||
	      !strcasecmp(value, "on") ||
	      !strcasecmp(value, "yes")) 
	    b = 1;
	  
          ippAddBoolean(request->ipp_request,
                        IPP_TAG_OPERATION,
                        option,
                        b);
        
          break;
        }
        
      case IPP_TAG_RANGE:
        {
          char	*s;
          int lower;
          int upper;
          
          if (*value == '-')
	    {
	      lower = 1;
	      s = value;
	    }
	  else
	    lower = strtol(value, &s, 0);

	  if (*s == '-')
	    {
	      if (s[1])
		upper = strtol(s + 1, NULL, 0);
	      else
		upper = 2147483647;
            }
	  else
	    upper = lower;
         
          ippAddRange (request->ipp_request,
                       IPP_TAG_OPERATION,
                       option,
                       lower,
                       upper);

          break;
        }

      case IPP_TAG_RESOLUTION:
        {
          char *s;
          int xres;
          int yres;
          ipp_res_t units;
          
          xres = strtol(value, &s, 0);

	  if (*s == 'x')
	    yres = strtol(s + 1, &s, 0);
	  else
	    yres = xres;

	  if (strcasecmp(s, "dpc") == 0)
            units = IPP_RES_PER_CM;
          else
            units = IPP_RES_PER_INCH;
          
          ippAddResolution (request->ipp_request,
                            IPP_TAG_OPERATION,
                            option,
                            units,
                            xres,
                            yres);

          break;
        }

      default:
        ippAddString (request->ipp_request,
                      IPP_TAG_OPERATION,
                      option_tag,
                      option,
                      NULL,
                      value);

        break;
    }
}
				

static void 
_post_send (EggCupsRequest *request)
{
  gchar length[255];
  struct stat data_info;

  if (request->data_fd != 0)
    {
      fstat (request->data_fd, &data_info);
      sprintf (length, "%lu", (unsigned long)ippLength(request->ipp_request) + data_info.st_size);
    }
  else
    {
      sprintf (length, "%lu", (unsigned long)ippLength(request->ipp_request));
    }
	
  httpClearFields(request->http);
  httpSetField(request->http, HTTP_FIELD_CONTENT_LENGTH, length);
  httpSetField(request->http, HTTP_FIELD_CONTENT_TYPE, "application/ipp");
  httpSetField(request->http, HTTP_FIELD_AUTHORIZATION, request->http->authstring);

  if (httpPost(request->http, request->resource))
    {
      if (httpReconnect(request->http))
        {
          g_warning ("failed Post");
          request->state = POST_DONE;

          egg_cups_result_set_error (request->result, "Failed Post");
        }

      request->attempts++;
      return;    
    }
        
    request->attempts = 0;

    request->state = POST_WRITE_REQUEST;
    request->ipp_request->state = IPP_IDLE;
}

static void 
_post_write_request (EggCupsRequest *request)
{
  ipp_state_t ipp_status;
  ipp_status = ippWrite(request->http, request->ipp_request);

  if (ipp_status == IPP_ERROR)
    {
      g_warning ("We got an error writting to the socket");
      request->state = POST_DONE;
     
      egg_cups_result_set_error (request->result, ippErrorString (cupsLastError ()));
      return;
    }

  if (ipp_status == IPP_DATA)
    {
      if (request->data_fd != 0)
        request->state = POST_WRITE_DATA;
      else
        request->state = POST_CHECK;
    }
}

static void 
_post_write_data (EggCupsRequest *request)
{
  ssize_t bytes;
  char buffer[_EGG_CUPS_MAX_CHUNK_SIZE];
  http_status_t http_status;

  if (httpCheck (request->http))
    http_status = httpUpdate(request->http);
  else
    http_status = request->last_status;

  request->last_status = http_status;

  if (http_status == HTTP_CONTINUE || http_status == HTTP_OK)
    {
      /* send data */
      bytes = read(request->data_fd, buffer, _EGG_CUPS_MAX_CHUNK_SIZE);
          
      if (bytes == 0)
        {
          request->state = POST_CHECK;
          request->attempts = 0;
          return;
        }

      if (httpWrite(request->http, buffer, (int)bytes) < bytes)
        {
          g_warning ("We got an error writting to the socket");
          request->state = POST_DONE;
     
          egg_cups_result_set_error (request->result, "Error writting to socket in Post");
          return;
        }
    }
   else
    {
      request->attempts++;
    }
}

static void 
_post_check (EggCupsRequest *request)
{
  http_status_t http_status;

  http_status = request->last_status;

  if (http_status == HTTP_CONTINUE)
    {
      goto again; 
    }
  else if (http_status == HTTP_UNAUTHORIZED)
    {
      /* TODO: callout for auth */
      g_warning ("NOT IMPLEMENTED: We need to prompt for authorization");
      request->state = POST_DONE;
     
      egg_cups_result_set_error (request->result, "Can't prompt for authorization");
      return;
    }
  else if (http_status == HTTP_ERROR)
    {
#ifdef G_OS_WIN32
      if (request->http->error != WSAENETDOWN && 
          request->http->error != WSAENETUNREACH)
#else
      if (request->http->error != ENETDOWN && 
          request->http->error != ENETUNREACH)
#endif /* G_OS_WIN32 */
        {
          goto again;
        }
      else
        {
          g_warning ("Error status");
          request->state = POST_DONE;
     
          egg_cups_result_set_error (request->result, "Unknown HTTP error");
          return;
        }
    }
/* TODO: detect ssl in configure.ac */
#if HAVE_SSL
  else if (http_status == HTTP_UPGRADE_REQUIRED)
    {
      /* Flush any error message... */
      httpFlush (request->http);

      /* Reconnect... */
      httpReconnect (request->http);

      /* Upgrade with encryption... */
      httpEncryption(request->http, HTTP_ENCRYPT_REQUIRED);
  
      goto again;
    }
#endif 
  else if (http_status != HTTP_OK)
    {
      g_warning ("Error status");
      request->state = POST_DONE;
     
      egg_cups_result_set_error (request->result, "HTTP Error");
      return;
    }
  else
    {
      request->state = POST_READ_RESPONSE;
      return;
    }

 again:
  request->attempts++;
  http_status = HTTP_CONTINUE;

  if (httpCheck (request->http))
    http_status = httpUpdate (request->http);

  request->last_status = http_status;
}

static void 
_post_read_response (EggCupsRequest *request)
{
  ipp_state_t ipp_status;

  if (request->result->ipp_response == NULL)
    request->result->ipp_response = ippNew();

  ipp_status = ippRead (request->http, 
                        request->result->ipp_response);

  if (ipp_status == IPP_ERROR)
    {
      g_warning ("Error reading response");
      egg_cups_result_set_error (request->result, ippErrorString (cupsLastError()));
      
      ippDelete (request->result->ipp_response);
      request->result->ipp_response = NULL;

      request->state = POST_DONE; 
    }
  else if (ipp_status == IPP_DATA)
    {
      request->state = POST_DONE;
    }
}

static void 
_get_send (EggCupsRequest *request)
{
  if (request->data_fd == 0)
    {
      egg_cups_result_set_error (request->result, "Get requires an open file descriptor");
      request->state = GET_DONE;

      return;
    }

  httpClearFields(request->http);
  httpSetField(request->http, HTTP_FIELD_AUTHORIZATION, request->http->authstring);

  if (httpGet(request->http, request->resource))
    {
      if (httpReconnect(request->http))
        {
          g_warning ("failed Get");
          request->state = GET_DONE;

          egg_cups_result_set_error (request->result, "Failed Get");
        }

      request->attempts++;
      return;    
    }
        
  request->attempts = 0;

  request->state = GET_CHECK;
  request->ipp_request->state = IPP_IDLE;
}

static void 
_get_check (EggCupsRequest *request)
{
  http_status_t http_status;

  http_status = request->last_status;

  if (http_status == HTTP_CONTINUE)
    {
      goto again; 
    }
  else if (http_status == HTTP_UNAUTHORIZED)
    {
      /* TODO: callout for auth */
      g_warning ("NOT IMPLEMENTED: We need to prompt for authorization in a non blocking manner");
      request->state = GET_DONE;
     
      egg_cups_result_set_error (request->result, "Can't prompt for authorization");
      return;
    }
/* TODO: detect ssl in configure.ac */
#if HAVE_SSL
  else if (http_status == HTTP_UPGRADE_REQUIRED)
    {
      /* Flush any error message... */
      httpFlush (request->http);

      /* Reconnect... */
      httpReconnect (request->http);

      /* Upgrade with encryption... */
      httpEncryption(request->http, HTTP_ENCRYPT_REQUIRED);
  
      goto again;
    }
#endif 
  else if (http_status != HTTP_OK)
    {
      g_warning ("Error status");
      request->state = POST_DONE;
     
      egg_cups_result_set_error (request->result, "HTTP Error");

      httpFlush(request->http);

      return;
    }
  else
    {
      request->state = GET_READ_DATA;
      return;
    }

 again:
  request->attempts++;
  http_status = HTTP_CONTINUE;

  if (httpCheck (request->http))
    http_status = httpUpdate (request->http);

  request->last_status = http_status;

}

static void 
_get_read_data (EggCupsRequest *request)
{
  char buffer[_EGG_CUPS_MAX_CHUNK_SIZE];
  int bytes;
  
  bytes = httpRead(request->http, buffer, sizeof(buffer));

  if (bytes == 0)
    {
      request->state = GET_DONE;
      return;
    }
    
  if (write (request->data_fd, buffer, bytes) == -1)
    {
      char *error_msg;

      request->state = POST_DONE;
    
      error_msg = strerror (errno);
      egg_cups_result_set_error (request->result, error_msg ? error_msg:""); 
    }
}

gboolean
egg_cups_request_is_done (EggCupsRequest *request)
{
  return (request->state == REQUEST_DONE);
}

gboolean
egg_cups_result_is_error (EggCupsResult *result)
{
  return result->is_error;
}

ipp_t *
egg_cups_result_get_response (EggCupsResult *result)
{
  return result->ipp_response;
}

const char *
egg_cups_result_get_error_string (EggCupsResult *result)
{
  return result->error_msg; 
}

