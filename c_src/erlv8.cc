#include "erlv8.hh"

typedef TickHandlerResolution (*TickHandler)(VM *, char *, ERL_NIF_TERM, ERL_NIF_TERM, ERL_NIF_TERM, int, const ERL_NIF_TERM*);

struct ErlV8TickHandler {
  const char * name;
  TickHandler handler;
};

static ErlV8TickHandler tick_handlers[] =
  {
    {"stop", StopTickHandler},
    {"result", ResultTickHandler},
    {"call", CallTickHandler},
    {"inst", InstantiateTickHandler},
    {"delete", DeleteTickHandler},
    {"taint", TaintTickHandler},
    {"equals", EqualsTickHandler},
    {"strict_equals", StrictEqualsTickHandler},
    {"get", GetTickHandler},
    {"get_proto", GetProtoTickHandler},
    {"get_hidden", GetHiddenTickHandler},
    {"set", SetTickHandler},
    {"set_proto", SetProtoTickHandler},
    {"set_hidden", SetHiddenTickHandler},
    {"set_accessor", SetAccessorTickHandler},
    {"proplist", ProplistTickHandler},
    {"list", ListTickHandler},
    {"script", ScriptTickHandler},
    {"gc", GCTickHandler},
    {"to_string", ToStringTickHandler},
    {"to_detail_string", ToDetailStringTickHandler},
    {"extern_proto", ExternProtoTickHandler},
    {"externalize", ExternalizeTickHandler},
    {"internal_count", InternalCountTickHandler},
    {"set_internal", SetInternalTickHandler},
    {"set_internal_extern", SetInternalTickHandler},
    {"get_internal", GetInternalTickHandler},
    {NULL, UnknownTickHandler} 
  };


VM::VM() {
  env = enif_alloc_env();
  mutex = enif_mutex_create((char*)"erlv8_vm_mutex");
  isolate = v8::Isolate::New();
  v8::Isolate::Scope iscope(isolate);
  v8::Locker locker(isolate);
  v8::HandleScope handle_scope;

  // Moved into the VM object since we have a own isolate for each VM
  global_template = v8::Persistent<v8::ObjectTemplate>::New(v8::ObjectTemplate::New());
  external_template = v8::Persistent<v8::ObjectTemplate>::New(v8::ObjectTemplate::New());
  empty_constructor = v8::Persistent<v8::FunctionTemplate>::New(v8::FunctionTemplate::New(EmptyFun));
  string__erlv8__ = v8::Persistent<v8::String>::New(v8::String::New("__erlv8__"));

  context = v8::Context::New(NULL, global_template);
  v8::Context::Scope context_scope(context);
  tid = enif_thread_self();

  context->Global()->SetHiddenValue(string__erlv8__,v8::External::New(this));
  
  ctx_res_t *ptr = (ctx_res_t *)enif_alloc_resource(ctx_resource, sizeof(ctx_res_t));
  ptr->ctx = v8::Persistent<v8::Context>::New(context);
  ERL_NIF_TERM resource_term = enif_make_resource(env, ptr);
  enif_release_resource(ptr);

  context->Global()->SetHiddenValue(v8::String::New("__erlv8__ctx__"),term_to_external(resource_term));

  v8::Local<v8::Object> tmp = external_template->NewInstance();
  external_proto_num = v8::Persistent<v8::Object>::New(tmp);
  external_proto_atom = v8::Persistent<v8::Object>::New(external_template->NewInstance());
  external_proto_bin = v8::Persistent<v8::Object>::New(external_template->NewInstance());
  external_proto_ref = v8::Persistent<v8::Object>::New(external_template->NewInstance());
  external_proto_fun = v8::Persistent<v8::Object>::New(external_template->NewInstance());
  external_proto_port = v8::Persistent<v8::Object>::New(external_template->NewInstance());
  external_proto_pid = v8::Persistent<v8::Object>::New(external_template->NewInstance());
  external_proto_tuple = v8::Persistent<v8::Object>::New(external_template->NewInstance());
  external_proto_list = v8::Persistent<v8::Object>::New(external_template->NewInstance());

  push_socket = zmq_socket(zmq_context, ZMQ_PUSH);
  ticker_push_socket = zmq_socket(zmq_context, ZMQ_PUSH);
  pull_socket = zmq_socket(zmq_context, ZMQ_PULL);

  char socket_id[64];
  sprintf(socket_id, "inproc://tick-publisher-%ld", (long int) this);

  char ticker_socket_id[64];
  sprintf(ticker_socket_id, "inproc://tick-publisher-ticker-%ld", (long int) this);

  zmq_bind(push_socket, socket_id);
  zmq_bind(ticker_push_socket, ticker_socket_id);
  zmq_connect(pull_socket, socket_id);
  zmq_connect(pull_socket, ticker_socket_id);
};

VM::~VM() { 
  //  v8::Isolate::Scope iscope(isolate);
  //  v8::Locker locker(isolate);
  //  v8::HandleScope handle_scope;
  isolate->Enter();

  TRACE("(%p) VM::~VM - 1\n", isolate);
  enif_mutex_destroy(mutex);
  TRACE("(%p) VM::~VM - 2\n", isolate);
  TRACE("(%p) VM::~VM - 3\n", isolate);
  //external_proto_bin.Dispose();
  TRACE("(%p) VM::~VM - 4\n", isolate);
  /*  external_proto_ref.Dispose();
  external_proto_fun.Dispose();
  external_proto_port.Dispose();
  external_proto_pid.Dispose();
  external_proto_tuple.Dispose();
  external_proto_list.Dispose();
  TRACE("(%p) VM::~VM - 4\n", isolate);
  global_template.Dispose();
  TRACE("(%p) VM::~VM - 5\n", isolate);
  external_template.Dispose();
  TRACE("(%p) VM::~VM - 6\n", isolate);
  empty_constructor.Dispose();
  TRACE("(%p) VM::~VM - 7\n", isolate);
  string__erlv8__.Dispose();
  TRACE("(%p) VM::~VM - 8\n", isolate);
  external_proto_num.Dispose();
  TRACE("(%p) VM::~VM - 9\n", isolate);
  external_proto_atom.Dispose();*/
  TRACE("(%p) VM::~VM - 10\n", isolate);
  enif_free_env(env);
  TRACE("(%p) VM::~VM - 11\n", isolate);
  //context.Dispose();
  while (v8::Isolate::GetCurrent() == isolate) {
    isolate->Exit();
  }
  // this should dispoe everything created in the isolate:
  // http://markmail.org/message/mcn27ibuijhgkehl
  isolate->Dispose();  
  
  zmq_close(push_socket);
  zmq_close(ticker_push_socket);
  zmq_close(pull_socket);
};

void VM::run() {
  v8::Isolate::Scope iscope(isolate);
  v8::Locker locker(isolate);
  v8::HandleScope handle_scope; // the very top level handle scope
  ticker(0);
};

void VM::terminate() {
  TRACE("(%p) VM::terminate - 1\n", isolate);
  v8::V8::TerminateExecution(isolate);

}

v8::Handle<v8::Value> VM::ticker(ERL_NIF_TERM ref0) {
  TRACE("(%p) VM::ticker - 0\n", isolate);
  LHCS(isolate, context);
  isolate->Enter();
  TRACE("(%p) VM::ticker - 1\n", isolate);
  char name[MAX_ATOM_LEN];
  unsigned len;
  
  ErlNifEnv * ref_env = enif_alloc_env();
  ERL_NIF_TERM ref;
  TRACE("(%p) VM::ticker - 2\n", isolate);
  if ((unsigned long) ref0 == 0) {
    ref = ref0;
    DEBUG(server, enif_make_atom(env, "current_ticker"), enif_make_atom(env, "top"));
  } else {
    ref = enif_make_copy(ref_env, ref0);
    DEBUG(server, enif_make_atom(env, "current_ticker"), enif_make_copy(env, ref));
  }
  TRACE("(%p) VM::ticker - 3\n", isolate);  
  
  zmq_msg_t msg;
  Tick tick_s;
  ERL_NIF_TERM tick, tick_ref;
  while (1) {
    {
      isolate->Exit();
      TRACE("(%p) VM::ticker - 3.1\n", isolate);  
      v8::Unlocker unlocker(isolate);
      TRACE("(%p) VM::ticker - 3.2\n", isolate);  
      zmq_msg_init (&msg);
      TRACE("(%p) VM::ticker - 3.3\n", isolate);  
      zmq_recv (pull_socket, &msg, 0);
      TRACE("(%p) VM::ticker - 3.4\n", isolate);
      memcpy(&tick_s, zmq_msg_data(&msg), sizeof(Tick));
      TRACE("(%p) VM::ticker - 3.5\n", isolate);
      tick = enif_make_copy(env, tick_s.tick);
      TRACE("(%p) VM::ticker - 3.6\n", isolate);
      tick_ref = enif_make_copy(env, tick_s.ref);
      TRACE("(%p) VM::ticker - 3.7\n", isolate);
      enif_free_env(tick_s.env);
      TRACE("(%p) VM::ticker - 3.8\n", isolate);
      zmq_msg_close(&msg);
      TRACE("(%p) VM::ticker - 3.9\n", isolate);
    }
    isolate->Enter();
    TRACE("(%p) VM::ticker - 4\n", isolate);
    DEBUG(server, 
	  enif_make_tuple2(env, 
			   enif_make_atom(env, "last_tick"), 
			   (unsigned long) ref == 0 ? enif_make_atom(env,"top") : enif_make_copy(env, ref)),
	  enif_make_copy(env, tick));
    
    if (enif_is_tuple(env, tick)) { // should be always true, just a sanity check
      TRACE("(%p) VM::ticker - 5\n", isolate);
      ERL_NIF_TERM *array;
      int arity;
      enif_get_tuple(env,tick,&arity,(const ERL_NIF_TERM **)&array);
      
      enif_get_atom_length(env, array[0], &len, ERL_NIF_LATIN1);
      enif_get_atom(env,array[0],(char *)&name,len + 1, ERL_NIF_LATIN1);
      
      // lookup the matrix
      unsigned int i = 0;
      bool stop_flag = false;
      TRACE("(%p) VM::ticker - 6 (%s)\n", isolate, name);
      while (!stop_flag) {
	if ((!tick_handlers[i].name) ||
	    (!strcmp(name,tick_handlers[i].name))) { // handler has been located
	  TRACE("(%p) VM::ticker - 7\n", isolate);
          TickHandlerResolution resolution = (tick_handlers[i].handler(this, name, tick, tick_ref, ref, arity, array));
	  TRACE("(%p) VM::ticker - 8\n", isolate);
	  
	  switch (resolution.type) {
	  case DONE:
	    stop_flag = true;
	    break;
	  case NEXT:
	    break;
	  case RETURN:
	    TRACE("(%p) VM::ticker - 9\n", isolate);
	    enif_free_env(ref_env);
	    TRACE("(%p) VM::ticker - 10\n", isolate);
	    enif_clear_env(env);
	    TRACE("(%p) VM::ticker - 11\n", isolate);
	    zmq_msg_t tick_msg;
	    int e;
	    TRACE("(%p) VM::ticker - 12\n", isolate);
	    while (!pop_ticks.empty()) {
	      TRACE("(%p) VM::ticker - 12.1\n", isolate);
	      Tick newtick = pop_ticks.front();
	      TRACE("(%p) VM::ticker - 12.2\n", isolate);
	      pop_ticks.pop();
	      TRACE("(%p) VM::ticker - 12.3\n", isolate);
	      zmq_msg_init_size(&tick_msg, sizeof(Tick));
	      TRACE("(%p) VM::ticker - 12.4\n", isolate);
              memcpy(zmq_msg_data(&tick_msg), &newtick, sizeof(Tick));
	      TRACE("(%p) VM::ticker - 12.5\n", isolate);
	      do {
		e = zmq_send(ticker_push_socket, &tick_msg, ZMQ_NOBLOCK);
		TRACE("(%p) VM::ticker - 12.6\n", isolate);
	      } while (e == EAGAIN);
	      zmq_msg_close(&tick_msg);
	    }
	    TRACE("(%p) VM::ticker - 13\n", isolate);
	    return handle_scope.Close(resolution.value);
	    break;
	  }
	}
	i++;
      }
    }
    enif_clear_env(env);
  }
};


void * start_vm(void *data) {
  VM *vm = reinterpret_cast<VM *>(data);
  vm->run();
  enif_mutex_lock(vm->mutex);
  enif_mutex_unlock(vm->mutex);
  delete vm;
  return NULL;
};


static ERL_NIF_TERM new_vm(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
  ERL_NIF_TERM term;
  VM *vm = new VM();
  
  vm_res_t *ptr = (vm_res_t *)enif_alloc_resource(vm_resource, sizeof(vm_res_t));

  ptr->vm = vm;
  vm->resource = ptr;
  
  term = enif_make_resource(env, ptr);
  
  enif_release_resource(ptr);
  
  return term;
};

static ERL_NIF_TERM set_server(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]) 
{
  vm_res_t *res;
  if (enif_get_resource(env,argv[0],vm_resource,(void **)(&res))) {
    res->vm->server = (ErlNifPid *) malloc(sizeof(ErlNifPid));
    enif_get_local_pid(env, argv[1], res->vm->server);
    enif_thread_create((char *)"erlv8", &res->vm->tid, start_vm, res->vm, NULL);
    return enif_make_atom(env,"ok");
  };
  return enif_make_badarg(env);
};

static ERL_NIF_TERM context(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]) 
{
  vm_res_t *res;
  if (enif_get_resource(env,argv[0],vm_resource,(void **)(&res))) {
    LHCS(res->vm->isolate, res->vm->context);
    
    ctx_res_t *ptr = (ctx_res_t *)enif_alloc_resource(ctx_resource, sizeof(ctx_res_t));
    ptr->ctx = v8::Persistent<v8::Context>::New(v8::Context::GetCurrent());
    
    ERL_NIF_TERM term = enif_make_resource(env, ptr);
    
    enif_release_resource(ptr);
    
    return term;
  };
  return enif_make_badarg(env);
};



static ERL_NIF_TERM kill(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]) {
  vm_res_t *res;
  TRACE("kill - 1\n");
  if (enif_get_resource(env,argv[0],vm_resource,(void **)(&res))) {
    TRACE("kill - 2\n");
    res->vm->terminate();
    TRACE("kill - 3\n");
    return enif_make_atom(env,"ok");
  } else {
    TRACE("kill - 3\n");
    return enif_make_badarg(env);
  } 
}

static ERL_NIF_TERM stop(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]) {
  vm_res_t *res;
  int e;
  if (enif_get_resource(env,argv[0],vm_resource,(void **)(&res))) {
    if ((!enif_is_ref(env, argv[1])))
      return enif_make_badarg(env);
    TRACE("(%p) stop\n", res->vm->isolate);
    zmq_msg_t tick_msg;
    
    Tick tick;
    tick.env = enif_alloc_env();
    tick.tick = enif_make_tuple1(tick.env, enif_make_atom(tick.env, "stop"));
    tick.ref = enif_make_copy(tick.env, argv[1]);
    
    
    zmq_msg_init_size(&tick_msg, sizeof(Tick));
    
    memcpy(zmq_msg_data(&tick_msg), &tick, sizeof(Tick));
    
    enif_mutex_lock(res->vm->mutex);
    do {
      e = zmq_send(res->vm->push_socket, &tick_msg, ZMQ_NOBLOCK);
    } while (e == EAGAIN);
    
    zmq_msg_close(&tick_msg);
    enif_mutex_unlock(res->vm->mutex);
    
    return enif_make_atom(env,"ok");
  } else {
    return enif_make_badarg(env);
  };
};

static ERL_NIF_TERM tick(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]) {
  vm_res_t *res;
  int e;
  if (enif_get_resource(env,argv[0],vm_resource,(void **)(&res))) {
    if ((!enif_is_ref(env, argv[1])))
      return enif_make_badarg(env);
    
    zmq_msg_t tick_msg;
    
    Tick tick;
    tick.env = enif_alloc_env();
    tick.tick = enif_make_copy(tick.env, argv[2]);
    tick.ref = enif_make_copy(tick.env, argv[1]);
    
    
    zmq_msg_init_size(&tick_msg, sizeof(Tick));
    
    memcpy(zmq_msg_data(&tick_msg), &tick, sizeof(Tick));
    
    do {
      e = zmq_send(res->vm->push_socket, &tick_msg, ZMQ_NOBLOCK);
    } while (e == EAGAIN);
    
    zmq_msg_close(&tick_msg);
    
    return enif_make_atom(env,"tack");
  } else {
    return enif_make_badarg(env);
  };
};

static ERL_NIF_TERM global(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]) {
  ctx_res_t *res;
  vm_res_t *vm_res;

  if (
      enif_get_resource(env, argv[0], vm_resource, (void **)(&vm_res))
      && enif_get_resource(env,argv[1],ctx_resource,(void **)(&res))) {
    LHCS(vm_res->vm->isolate, res->ctx);
    v8::Handle<v8::Object> global = res->ctx->Global();
    return js_to_term(res->ctx, vm_res->vm->isolate, env,global);
  } else {
    return enif_make_badarg(env);
  };
};

static ERL_NIF_TERM new_context(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]) {
  vm_res_t *res;
  if (enif_get_resource(env, argv[0], vm_resource, (void **)(&res))) {
    LHCS(res->vm->isolate, res->vm->context);
    v8::Persistent<v8::Context> context = v8::Context::New(NULL, res->vm->global_template);
    context->Global()->SetHiddenValue(res->vm->string__erlv8__, v8::External::New(res->vm));
    
    ctx_res_t *ptr = (ctx_res_t *)enif_alloc_resource(ctx_resource, sizeof(ctx_res_t));
    ptr->ctx = v8::Persistent<v8::Context>::New(context);
    
    ERL_NIF_TERM resource_term = enif_make_resource(env, ptr);
    
    enif_release_resource(ptr);
    
    context->Global()->SetHiddenValue(v8::String::New("__erlv8__ctx__"), term_to_external(resource_term));
    
    return resource_term;
  } else {
    return enif_make_badarg(env);
  };
};

static ErlNifFunc nif_funcs[] =
  {
    {"kill", 1, kill},
    {"new_vm", 0, new_vm},
    {"set_server", 2, set_server},
    {"context", 1, context},
    {"new_context", 1, new_context},
    {"global", 2, global},
    {"tick", 3, tick},
    {"stop", 2, stop},
  };

v8::Handle<v8::Value> EmptyFun(const v8::Arguments &arguments) {
  v8::HandleScope handle_scope;
  return v8::Undefined();
}

v8::Handle<v8::Value> WrapFun(const v8::Arguments &arguments) {
  v8::HandleScope handle_scope;
  VM * vm = (VM *)__ERLV8__(v8::Context::GetCurrent()->Global());
  {
    LHCS(vm->isolate, vm->context);
    
    // each call gets a unique ref
    ERL_NIF_TERM ref = enif_make_ref(vm->env);
    // prepare arguments
    ERL_NIF_TERM *arr = (ERL_NIF_TERM *) malloc(sizeof(ERL_NIF_TERM) * arguments.Length());
    for (int i=0;i<arguments.Length();i++) {
      arr[i] = js_to_term(vm->context, vm->isolate, vm->env,arguments[i]);
    }
    ERL_NIF_TERM arglist = enif_make_list_from_array(vm->env,arr,arguments.Length());
    free(arr);
    // send invocation request
    SEND(vm->server,
	 enif_make_tuple3(env,
			  enif_make_copy(env,external_to_term(arguments.Data())),
			  enif_make_tuple7(env, 
					   enif_make_atom(env,"erlv8_fun_invocation"),
					   enif_make_atom(env,arguments.IsConstructCall() ? "true" : "false"),
					   js_to_term(vm->context, vm->isolate, env, arguments.Holder()),
					   js_to_term(vm->context, vm->isolate, env, arguments.This()),
					   enif_make_copy(env, ref),
					   enif_make_pid(env, vm->server),
					   enif_make_copy(env, external_to_term(v8::Context::GetCurrent()->Global()->GetHiddenValue(v8::String::New("__erlv8__ctx__"))))),
			  enif_make_copy(env,arglist)));
    return handle_scope.Close(vm->ticker(ref));
  }
};


static void vm_resource_destroy(ErlNifEnv* env, void* obj) {
};

static void val_resource_destroy(ErlNifEnv* env, void* obj) {
  // v8::Locker locker;
  //  val_res_t * res = reinterpret_cast<val_res_t *>(obj);
  // res->ctx.Dispose();
  // res->val.Dispose();
};

static void ctx_resource_destroy(ErlNifEnv* env, void* obj) {
  // v8::Locker locker;
  // ctx_res_t * res = reinterpret_cast<ctx_res_t *>(obj);
  //res->ctx.Dispose();
};


int load(ErlNifEnv *env, void** priv_data, ERL_NIF_TERM load_info)
{
  TRACE("load\n");
  zmq_context = zmq_init(0); // we are using inproc only, so no I/O threads
  
  vm_resource = enif_open_resource_type(env, NULL, "erlv8_vm_resource", vm_resource_destroy, (ErlNifResourceFlags) (ERL_NIF_RT_CREATE | ERL_NIF_RT_TAKEOVER), NULL);
  val_resource = enif_open_resource_type(env, NULL, "erlv8_val_resource", val_resource_destroy, (ErlNifResourceFlags) (ERL_NIF_RT_CREATE | ERL_NIF_RT_TAKEOVER), NULL);
  ctx_resource = enif_open_resource_type(env, NULL, "erlv8_ctx_resource", ctx_resource_destroy, (ErlNifResourceFlags) (ERL_NIF_RT_CREATE | ERL_NIF_RT_TAKEOVER), NULL);

  v8::V8::Initialize();
  int preemption = 100; // default value
  enif_get_int(env, load_info, &preemption);
  v8::Locker locker;
  v8::Locker::StartPreemption(preemption);

  v8::HandleScope handle_scope;


  return 0;
};

void unload(ErlNifEnv *env, void* priv_data)
{
  TRACE("unload\n");
  v8::Locker::StopPreemption();
  zmq_term(zmq_context);
};

int reload(ErlNifEnv* env, void** priv_data, ERL_NIF_TERM load_info) {
  return 0;
}

int upgrade(ErlNifEnv* env, void** priv_data, void** old_priv_data, ERL_NIF_TERM load_info) {
  return 0;
}

ErlNifResourceType * ctx_resource;
ErlNifResourceType * vm_resource;
ErlNifResourceType * val_resource;

void *zmq_context;

ERL_NIF_INIT(erlv8_nif,nif_funcs,load,reload,upgrade,unload)
