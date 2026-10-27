/* Port of src/compat/qc/mod-commands.ts. GPL-2.0-or-later. */
#include "qa/compat/qc/mod_commands.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
static qa_mod_callback_value resolve(qa_mod_console_value v, const char *const *argv, size_t argc, const char *args_text) {
  qa_mod_callback_value r; memset(&r,0,sizeof r);
  if (strcmp(v.kind,"argument")==0) {
    if (!(v.index>=0 && v.index==trunc(v.index) && v.index<=9007199254740991.0)) qa_range_error("Mod console argument index must be nonnegative");
    const char *text = (size_t)v.index < argc ? argv[(size_t)v.index] : "";
    if (v.type && strcmp(v.type,"string")==0) { r.kind="string"; r.string_value=text; } else { r.kind="float"; r.number_value=qa_native_atof(text); }
    return r;
  }
  if (strcmp(v.kind,"arguments-text")==0) { r.kind="string"; r.string_value=args_text; return r; }
  if (strcmp(v.kind,"argument-count")==0) { r.kind="float"; r.number_value=(double)argc; return r; }
  r.kind=v.kind; r.number_value=v.number_value; r.string_value=v.string_value; r.vector_value=v.vector_value; return r;
}
bool qa_compat_qc_console_call(qa_mod_console_command cmd, const char *const *argv, size_t argc, const char *args_text, qa_mod_source_call *out, qa_mod_callback_value **out_args, qa_mod_named_value **out_globals) {
  qa_mod_callback_value *args = malloc((cmd.argument_count?cmd.argument_count:1)*sizeof *args);
  qa_mod_named_value *globals = malloc((cmd.global_count?cmd.global_count:1)*sizeof *globals);
  if(!args||!globals){free(args);free(globals);return false;}
  for(size_t i=0;i<cmd.argument_count;i++) args[i]=resolve(cmd.arguments[i],argv,argc,args_text);
  for(size_t i=0;i<cmd.global_count;i++) globals[i]=(qa_mod_named_value){cmd.globals[i].name, resolve(cmd.globals[i].value,argv,argc,args_text)};
  *out=(qa_mod_source_call){cmd.function,args,cmd.argument_count,globals,cmd.global_count};
  *out_args=args; *out_globals=globals; return true;
}
void qa_compat_qc_console_call_destroy(qa_mod_callback_value *a, qa_mod_named_value *g){free(a);free(g);}
