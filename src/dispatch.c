#include "dispatch.h"
#include <string.h>
lb_result cli_dispatch(lb_client *client,const char *operation,const char *arguments,char **output) {
 const char *allowed[]={"lease","require","heartbeat","status","sign_in","logout","modules","request","requests","access-link","record"};
 *output=NULL;
 for(size_t i=0;i<sizeof(allowed)/sizeof(*allowed);i++)
  if(!strcmp(operation,allowed[i]))return lb_client_call_json(client,operation,arguments,output);
 return LB_INVALID_ARGUMENT;
}
