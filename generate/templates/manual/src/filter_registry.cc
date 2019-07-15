#include <nan.h>
#include <string.h>

extern "C" {
  #include <git2.h>
}

#include "../include/nodegit.h"
#include "../include/lock_master.h"
#include "../include/functions/copy.h"
#include "../include/filter_registry.h"
#include "nodegit_wrapper.cc"
#include "../include/async_libgit2_queue_worker.h"

#include "../include/filter.h"

using namespace std;
using namespace v8;
using namespace node;

Nan::Persistent<v8::Object> GitFilterRegistry::persistentHandle;

// #pragma unmanaged
void GitFilterRegistry::InitializeComponent(v8::Local<v8::Object> target) {
  Nan::HandleScope scope;

  v8::Local<Object> object = Nan::New<Object>();

  Nan::SetMethod(object, "register", GitFilterRegister);
  Nan::SetMethod(object, "unregister", GitFilterUnregister);

  Nan::Set(target, Nan::New<String>("FilterRegistry").ToLocalChecked(), object);
  GitFilterRegistry::persistentHandle.Reset(object);
}

NAN_METHOD(GitFilterRegistry::GitFilterRegister) {
  Nan::EscapableHandleScope scope;

  if (info.Length() == 0 || !info[0]->IsString()) {
    return Nan::ThrowError("String name is required.");
  }

  if (info.Length() == 1 || !info[1]->IsObject()) {
    return Nan::ThrowError("Filter filter is required.");
  }

  if (info.Length() == 2 || !info[2]->IsNumber()) {
    return Nan::ThrowError("Number priority is required.");
  }

  if (info.Length() == 3 || !info[3]->IsFunction()) {
    return Nan::ThrowError("Callback is required and must be a Function.");
  }

  FilterRegisterBaton *baton = new FilterRegisterBaton;

  baton->filter = Nan::ObjectWrap::Unwrap<GitFilter>(Nan::To<v8::Object>(info[1]).ToLocalChecked())->GetValue();
  Nan::Utf8String name(Nan::To<v8::String>(info[0]).ToLocalChecked());

  baton->filter_name = (char *)malloc(name.length() + 1);
  memcpy((void *)baton->filter_name, *name, name.length());
  memset((void *)(((char *)baton->filter_name) + name.length()), 0, 1);

  baton->error_code = GIT_OK;
  baton->filter_priority = Nan::To<int>(info[2]).FromJust();

  Nan::Set(Nan::New(GitFilterRegistry::persistentHandle), Nan::To<v8::String>(info[0]).ToLocalChecked(), Nan::To<v8::Object>(info[1]).ToLocalChecked());

  Nan::Callback *callback = new Nan::Callback(Local<Function>::Cast(info[3]));
  RegisterWorker *worker = new RegisterWorker(baton, callback);

  worker->SaveToPersistent("filter_name", Nan::To<v8::Object>(info[0]).ToLocalChecked());
  worker->SaveToPersistent("filter_priority", Nan::To<v8::Object>(info[2]).ToLocalChecked());

  AsyncLibgit2QueueWorker(worker);
  return;
}

void GitFilterRegistry::RegisterWorker::Execute() {
  git_error_clear();

  {
    LockMaster lockMaster(/*asyncAction: */true, baton->filter_name, baton->filter);
    int result = git_filter_register(baton->filter_name, baton->filter, baton->filter_priority);
    baton->error_code = result;

    if (result != GIT_OK && git_error_last() != NULL) {
      baton->error = git_error_dup(git_error_last());
    }
  }
}

void GitFilterRegistry::RegisterWorker::HandleOKCallback() {
  if (baton->error_code == GIT_OK) {
    v8::Local<v8::Value> result = Nan::New(baton->error_code);
    v8::Local<v8::Value> argv[2] = {
      Nan::Null(),
      result
    };
    callback->Call(2, argv, async_resource);
  }
  else if (baton->error) {
    v8::Local<v8::Object> err;
    if (baton->error->message) {
      err = Nan::To<v8::Object>(Nan::Error(baton->error->message)).ToLocalChecked();
    } else {
      err = Nan::To<v8::Object>(Nan::Error("Method register has thrown an error.")).ToLocalChecked();
    }
    Nan::Set(err, Nan::New("errno").ToLocalChecked(), Nan::New(baton->error_code));
    Nan::Set(err, Nan::New("errorFunction").ToLocalChecked(), Nan::New("FilterRegistry.register").ToLocalChecked());
    v8::Local<v8::Value> argv[1] = {
      err
    };
    callback->Call(1, argv, async_resource);
    if (baton->error->message)
      free((void *)baton->error->message);
    free((void *)baton->error);
  }
  else if (baton->error_code < 0) {
    v8::Local<v8::Object> err = Nan::To<v8::Object>(Nan::Error("Method register has thrown an error.")).ToLocalChecked();
    Nan::Set(err, Nan::New("errno").ToLocalChecked(), Nan::New(baton->error_code));
    Nan::Set(err, Nan::New("errorFunction").ToLocalChecked(), Nan::New("FilterRegistry.register").ToLocalChecked());
    v8::Local<v8::Value> argv[1] = {
      err
    };
    callback->Call(1, argv, async_resource);
  }
  else {
    callback->Call(0, NULL, async_resource);
  }
  delete baton;
  return;
}

NAN_METHOD(GitFilterRegistry::GitFilterUnregister) {
  Nan::EscapableHandleScope scope;

  if (info.Length() == 0 || !info[0]->IsString()) {
    return Nan::ThrowError("String name is required.");
  }

  if (info.Length() == 1 || !info[1]->IsFunction()) {
    return Nan::ThrowError("Callback is required and must be a Function.");
  }

  FilterUnregisterBaton *baton = new FilterUnregisterBaton;
  Nan::Utf8String name(Nan::To<v8::String>(info[0]).ToLocalChecked());

  baton->filter_name = (char *)malloc(name.length() + 1);
  memcpy((void *)baton->filter_name, *name, name.length());
  memset((void *)(((char *)baton->filter_name) + name.length()), 0, 1);

  baton->error_code = GIT_OK;

  /* Setting up Async Worker */
  Nan::Callback *callback = new Nan::Callback(Local<Function>::Cast(info[1]));
  UnregisterWorker *worker = new UnregisterWorker(baton, callback);

  worker->SaveToPersistent("filter_name", info[0]);

  AsyncLibgit2QueueWorker(worker);
  return;
}

void GitFilterRegistry::UnregisterWorker::Execute() {
  git_error_clear();

  {
    LockMaster lockMaster(/*asyncAction: */true, baton->filter_name);
    int result = git_filter_unregister(baton->filter_name);
    baton->error_code = result;

    if (result != GIT_OK && git_error_last() != NULL) {
      baton->error = git_error_dup(git_error_last());
    }
  }
}

void GitFilterRegistry::UnregisterWorker::HandleOKCallback() {
  if (baton->error_code == GIT_OK) {
    v8::Local<v8::Value> result = Nan::New(baton->error_code);
    v8::Local<v8::Value> argv[2] = {
      Nan::Null(),
      result
    };
    callback->Call(2, argv, async_resource);
  }
  else if (baton->error) {
    v8::Local<v8::Object> err;
    if (baton->error->message) {
      err = Nan::To<v8::Object>(Nan::Error(baton->error->message)).ToLocalChecked();
    } else {
      err = Nan::To<v8::Object>(Nan::Error("Method register has thrown an error.")).ToLocalChecked();
    }
    Nan::Set(err, Nan::New("errno").ToLocalChecked(), Nan::New(baton->error_code));
    Nan::Set(err, Nan::New("errorFunction").ToLocalChecked(), Nan::New("FilterRegistry.unregister").ToLocalChecked());
    v8::Local<v8::Value> argv[1] = {
      err
    };
    callback->Call(1, argv, async_resource);
    if (baton->error->message)
      free((void *)baton->error->message);
    free((void *)baton->error);
  }
  else if (baton->error_code < 0) {
    v8::Local<v8::Object> err = Nan::To<v8::Object>(Nan::Error("Method unregister has thrown an error.")).ToLocalChecked();
    Nan::Set(err, Nan::New("errno").ToLocalChecked(), Nan::New(baton->error_code));
    Nan::Set(err, Nan::New("errorFunction").ToLocalChecked(), Nan::New("FilterRegistry.unregister").ToLocalChecked());
    v8::Local<v8::Value> argv[1] = {
      err
    };
    callback->Call(1, argv, async_resource);
  }
  else {
    callback->Call(0, NULL, async_resource);
  }
  delete baton;
  return;
}
