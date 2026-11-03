#include "/home/buzzkill/Projects/quake-anthology-c/src/app/bootstrap/network/unified_event_codec.c"
#include <sys/wait.h>
#include <unistd.h>
#include <sys/prctl.h>
static qa_actor_id a(void *ctx,double s,double g){(void)ctx;qa_actor_id r={0};r.slot=(int64_t)s+10;r.generation=(int64_t)g+1;return r;}
static qa_client_id cl(void *ctx,double s,double g){(void)ctx;qa_client_id r={0};r.slot=(int64_t)s+20;r.generation=(int64_t)g+2;return r;}
static qa_seat_id se(void *ctx,double i){(void)ctx;qa_seat_id r={0};r.index=(int64_t)i+1;return r;}
static qa_resource_id re(void *ctx,qa_resource_id id){(void)ctx;(void)id;return "resource:unified:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";}
static qa_app_unified_identity_decoder idn={.actor=a,.client=cl,.seat=se,.resource_id=re};
static void one(const char *line){
  qa_save_value *row=qa_parse_json_value((const uint8_t*)line,strlen(line),false);
  const qa_save_value *root=qa_save_value_get(row,"root"),*event=qa_save_value_get(row,"event");
  if(strcmp(root->text,"presentation")==0){
    qa_save_value *list=ev_array();ev_push(list,ev_clone(event));
    buffer b={0};json_write(&b,list);
    size_t n;qa_save_value **ev=qa_app_unified_decode_presentation_events((uint8_t*)b.data,b.length,&idn,&n);
    size_t len;uint8_t *o=qa_app_unified_encode_presentation_events(ev,n,&len);
    printf("OK %.*s\n",(int)len,o);
  } else {
    qa_save_value *d=qa_app_unified_read_simulation_event(event,&idn);
    qa_save_value *w=qa_app_unified_write_simulation_event(d);
    buffer b={0};json_write(&b,w);
    printf("OK %s\n",b.data?b.data:"");
  }
}
int main(int c,char**v){
  FILE*f=fopen(v[1],"r");FILE*out=fopen(v[2],"w");
  static char line[1<<20];
  while(fgets(line,sizeof line,f)){
    line[strcspn(line,"\n")]=0;
    int pp[2],ee[2];pipe(pp);pipe(ee);
    pid_t p=fork();
    if(!p){prctl(PR_SET_DUMPABLE,0);dup2(pp[1],1);dup2(ee[1],2);close(pp[0]);close(ee[0]);one(line);fflush(stdout);_exit(0);}
    close(pp[1]);close(ee[1]);
    static char buf[1<<22];size_t n=0;ssize_t r;
    while((r=read(pp[0],buf+n,sizeof buf-n-1))>0)n+=(size_t)r;
    buf[n]=0;
    static char ebuf[1<<16];size_t en=0;
    while((r=read(ee[0],ebuf+en,sizeof ebuf-en-1))>0)en+=(size_t)r;
    ebuf[en]=0;close(pp[0]);close(ee[0]);
    int st;waitpid(p,&st,0);
    if(WIFEXITED(st)&&WEXITSTATUS(st)==0)fputs(buf,out);
    else{ebuf[strcspn(ebuf,"\n")]=0;fprintf(out,"ERR %s\n",ebuf);}
  }
  fclose(out);(void)c;
  return 0;
}
