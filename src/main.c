#define _POSIX_C_SOURCE 200809L
#include "dispatch.h"
#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <math.h>
#include <errno.h>
typedef struct json_object J;
static volatile sig_atomic_t interrupted;
static void interrupt_handler(int signal_number){(void)signal_number;interrupted=1;}
static int cancelled(void *context){(void)context;return interrupted!=0;}
static void safe_text(const char *text){
 for(const unsigned char *p=(const unsigned char*)text;*p;p++) {
  if(*p<32||*p==127)fprintf(stderr,"\\x%02x",*p);else fputc(*p,stderr);
 }
}
static void signin(void *context,const char *url,const char *code){
 (void)context;fputs("Sign in: ",stderr);safe_text(url);fputs("\nCode: ",stderr);safe_text(code);fputc('\n',stderr);
}
static int failure(const char *code,int exit_code){fprintf(stderr,"licensebox: %s\n",code);return exit_code;}
static void usage(void){
 puts("Usage: licensebox --product PRODUCT --keys PUBLIC_KEYS.json [options] COMMAND [arguments]\n"
      "Options: --server URL --directory DIR --version APP_VERSION --ca-bundle PEM\n"
      "Commands:\n"
      "  require MODULE                 Print authorized only after a successful check\n"
      "  heartbeat MODULE               Force signed renewal; print authorized only\n"
      "  status                         Print local verified product/modules/expiry\n"
      "  sign-in [--timeout SECONDS]     Display sign-in URI and code on stderr\n"
      "  logout                         Retain installation key; clear authorization\n"
      "  modules | requests             Print server response JSON\n"
      "  request MODULE... [--note TEXT]\n"
      "  access-link                    Explicitly print a private access URL\n"
      "  record COMMAND (--success|--failed) --elapsed SECONDS\n"
      "This program authorizes access; the calling shell controls subsequent work.");
}
static J *read_keys(const char *path){
 FILE *f=fopen(path,"rb");if(!f)return NULL;
 char *data=malloc((1u<<20)+1);if(!data){fclose(f);return NULL;}
 size_t n=fread(data,1,(1u<<20)+1,f);int bad=ferror(f)||n>(1u<<20);fclose(f);
 if(bad||memchr(data,0,n)){free(data);return NULL;}
 data[n]=0;struct json_tokener *tok=json_tokener_new();if(!tok){free(data);return NULL;}
 J *value=json_tokener_parse_ex(tok,data,(int)n);
 size_t end=json_tokener_get_parse_end(tok);
 while(end<n&&(data[end]==' '||data[end]=='\n'||data[end]=='\r'||data[end]=='\t'))end++;
 if(json_tokener_get_error(tok)!=json_tokener_success||end!=n||!json_object_is_type(value,json_type_object)){json_object_put(value);value=NULL;}
 json_tokener_free(tok);free(data);return value;
}
static int number(const char *text,double *out){
 char *end=NULL;errno=0;*out=strtod(text,&end);
 return text[0]&&end&&!*end&&!errno&&isfinite(*out)&&*out>=0;
}
static void add_string(J *o,const char *key,const char *value){json_object_object_add(o,key,json_object_new_string(value));}
static int output_status(const char *text){
 J *lease=json_tokener_parse(text);if(!json_object_is_type(lease,json_type_object)){json_object_put(lease);return failure("protocol_error",1);}
 J *summary=json_object_new_object();const char *fields[]={"product","modules","expires"};
 for(size_t i=0;i<3;i++){J *v=NULL;if(!json_object_object_get_ex(lease,fields[i],&v)){json_object_put(lease);json_object_put(summary);return failure("protocol_error",1);}json_object_object_add(summary,fields[i],json_object_get(v));}
 puts(json_object_to_json_string_ext(summary,JSON_C_TO_STRING_PLAIN));json_object_put(summary);json_object_put(lease);return 0;
}
int main(int argc,char **argv){
 if(argc==2&&!strcmp(argv[1],"--help")){usage();return 0;}
 if(argc==2&&!strcmp(argv[1],"--sdk-version")){puts("licensebox-cli 0.2.0 (native C 0.4.0)");return 0;}
 int i=1,exit_code=2;const char *keys_path=NULL,*product=NULL,*command=NULL,*operation=NULL;
 J *options=json_object_new_object(),*args=json_object_new_object(),*keys=NULL;
 lb_client *client=NULL;char *output=NULL;
 while(i<argc&&argv[i][0]=='-'){
  const char *name=argv[i++];if(i>=argc)goto invalid;
  const char *value=argv[i++],*field=NULL;
  if(!strcmp(name,"--product")){if(product)goto invalid;product=value;field="product";}
  else if(!strcmp(name,"--keys")){if(keys_path)goto invalid;keys_path=value;continue;}
  else if(!strcmp(name,"--server"))field="server";
  else if(!strcmp(name,"--directory"))field="directory";
  else if(!strcmp(name,"--version"))field="version";
  else if(!strcmp(name,"--ca-bundle"))field="ca_bundle";
  else goto invalid;
  J *old=NULL;if(json_object_object_get_ex(options,field,&old))goto invalid;
  add_string(options,field,value);
 }
 if(!product||!keys_path||i>=argc)goto invalid;
 command=argv[i++];operation=command;
 if(!strcmp(command,"require")||!strcmp(command,"heartbeat")){
  if(i+1!=argc)goto invalid;add_string(args,"module",argv[i++]);
 }else if(!strcmp(command,"sign-in")){
  operation="sign_in";double timeout=120;
  if(i<argc){if(i+2!=argc||strcmp(argv[i],"--timeout")||!number(argv[i+1],&timeout)||timeout<=0)goto invalid;i+=2;}
  json_object_object_add(args,"timeout",json_object_new_double(timeout));
 }else if(!strcmp(command,"request")){
  J *modules=json_object_new_array();json_object_object_add(args,"modules",modules);int noted=0;
  while(i<argc){
   if(!strcmp(argv[i],"--note")){if(noted||i+1>=argc)goto invalid;noted=1;add_string(args,"note",argv[i+1]);i+=2;}
   else if(argv[i][0]=='-')goto invalid;
   else json_object_array_add(modules,json_object_new_string(argv[i++]));
  }
  if(!json_object_array_length(modules))goto invalid;
 }else if(!strcmp(command,"record")){
  if(i>=argc||argv[i][0]=='-')goto invalid;add_string(args,"command",argv[i++]);
  int success=-1;double elapsed=-1;
  while(i<argc){
   if(!strcmp(argv[i],"--success")||!strcmp(argv[i],"--failed")){
    if(success>=0)goto invalid;success=!strcmp(argv[i++],"--success");
   }else if(!strcmp(argv[i],"--elapsed")){
    if(elapsed>=0||i+1>=argc||!number(argv[i+1],&elapsed))goto invalid;i+=2;
   }else goto invalid;
  }
  if(success<0||elapsed<0)goto invalid;
  json_object_object_add(args,"success",json_object_new_boolean(success));json_object_object_add(args,"elapsed",json_object_new_double(elapsed));
 }else if(strcmp(command,"status")&&strcmp(command,"logout")&&strcmp(command,"modules")&&strcmp(command,"requests")&&strcmp(command,"access-link"))goto invalid;
 if(i!=argc)goto invalid;
 keys=read_keys(keys_path);if(!keys){exit_code=failure("invalid_argument",2);goto cleanup;}
 json_object_object_add(options,"keys",keys);keys=NULL;
 lb_callbacks callbacks={.on_sign_in=signin,.cancelled=cancelled};
 struct sigaction action;memset(&action,0,sizeof(action));action.sa_handler=interrupt_handler;sigemptyset(&action.sa_mask);sigaction(SIGINT,&action,NULL);sigaction(SIGTERM,&action,NULL);
 lb_result r=lb_client_create_json(json_object_to_json_string_ext(options,JSON_C_TO_STRING_PLAIN),&callbacks,&client);
 if(r==LB_OK)r=cli_dispatch(client,operation,json_object_to_json_string_ext(args,JSON_C_TO_STRING_PLAIN),&output);
 if(r!=LB_OK){
  const char *code=lb_result_code(r);
  if(client&&r!=LB_INVALID_ARGUMENT&&strcmp(lb_error_code(client),"ok"))code=lb_error_code(client);
  exit_code=failure(code,r==LB_INVALID_ARGUMENT?2:r==LB_CANCELLED?130:1);goto cleanup;
 }
 if(!output){exit_code=failure("protocol_error",1);goto cleanup;}
 exit_code=0;
 if(!strcmp(command,"require")||!strcmp(command,"heartbeat")||!strcmp(command,"sign-in"))puts("authorized");
 else if(!strcmp(command,"status"))exit_code=output_status(output);
 else if(!strcmp(command,"access-link")){
  J *url=json_tokener_parse(output);if(!json_object_is_type(url,json_type_string))exit_code=failure("protocol_error",1);else puts(json_object_get_string(url));json_object_put(url);
 }else if(!strcmp(command,"logout"))puts("logged_out");
 else if(!strcmp(command,"record"))puts("recorded");
 else puts(output);
 goto cleanup;
invalid:
 exit_code=failure("invalid_argument",2);
cleanup:
 lb_free(output);lb_client_destroy(client);json_object_put(keys);json_object_put(options);json_object_put(args);return exit_code;
}
