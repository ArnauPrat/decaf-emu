#pragma once
#include "common/type_list.h"
#include "decaf_config.h"
#include "kernel_hlesymbol.h"
#include "libcpu/state.h"
#include "ppcutils/ppcinvoke.h"
#include <cstdint>

namespace kernel
{

struct HleFunction : HleSymbol
{
   HleFunction() :
      HleSymbol(HleSymbol::Function)
   {
   }

   virtual ~HleFunction() override = default;
   virtual void call(cpu::Core *state) = 0;

   bool valid = false;
   bool traceEnabled = true;
   uint32_t syscallID = 0;
   uint32_t vaddr = 0;
};

namespace functions
{

void kcTraceHandler(const std::string& str);

template<typename ReturnType, typename... Args>
struct HleFunctionImpl : HleFunction
{
   ReturnType (*wrapped_function)(Args...);

   virtual void call(cpu::Core *thread) override
   {
      if (decaf::config::log::kernel_trace && traceEnabled) {
         if (decaf::config::log::kernel_trace_res) {
            ppctypes::invoke(kcTraceHandler, kcTraceHandler, thread, wrapped_function, name);
         } else {
            ppctypes::invoke(kcTraceHandler, nullptr, thread, wrapped_function, name);
         }
      } else {
         ppctypes::invoke(nullptr, nullptr, thread, wrapped_function, name);
      }
   }
};

template<typename ReturnType, typename ObjectType, typename... Args>
struct HleMemberFunctionImpl : HleFunction
{
   ReturnType (ObjectType::*wrapped_function)(Args...);

   virtual void call(cpu::Core *thread) override
   {
      if (decaf::config::log::kernel_trace && traceEnabled) {
         if (decaf::config::log::kernel_trace_res) {
            ppctypes::invokeMemberFn(kcTraceHandler, kcTraceHandler, thread, wrapped_function, name);
         } else {
            ppctypes::invokeMemberFn(kcTraceHandler, nullptr, thread, wrapped_function, name);
         }
      } else {
         ppctypes::invokeMemberFn(nullptr, nullptr, thread, wrapped_function, name);
      }
   }
};

template<typename ObjectType, typename... Args>
struct HleConstructorFunctionImpl : HleFunction
{
   static void trampFunction(ObjectType *object, Args... args)
   {
      new (object) ObjectType(args...);
   }

   virtual void call(cpu::Core *thread) override
   {
      if (decaf::config::log::kernel_trace && traceEnabled) {
         if (decaf::config::log::kernel_trace_res) {
            ppctypes::invoke(kcTraceHandler, kcTraceHandler, thread, &trampFunction, name);
         } else {
            ppctypes::invoke(kcTraceHandler, nullptr, thread, &trampFunction, name);
         }
      } else {
         ppctypes::invoke(nullptr, nullptr, thread, &trampFunction, name);
      }
   }
};

template<typename ObjectType>
struct HleDestructorFunctionImpl : HleFunction
{
   static void trampFunction(ObjectType *object)
   {
      object->~ObjectType();
   }

   virtual void call(cpu::Core *thread) override
   {
      if (decaf::config::log::kernel_trace && traceEnabled) {
         if (decaf::config::log::kernel_trace_res) {
            ppctypes::invoke(kcTraceHandler, kcTraceHandler, thread, &trampFunction, name);
         } else {
            ppctypes::invoke(kcTraceHandler, nullptr, thread, &trampFunction, name);
         }
      } else {
         ppctypes::invoke(nullptr, nullptr, thread, &trampFunction, name);
      }
   }
};

} // namespace functions

// Regular Function
template<typename ReturnType, typename... Args>
inline HleFunction *
makeFunction(ReturnType (*fptr)(Args...), void *hostPtr = nullptr)
{
   auto func = new kernel::functions::HleFunctionImpl<ReturnType, Args...>();
   func->valid = true;
   func->wrapped_function = fptr;
   func->hostPtr = hostPtr;
   return func;
}

// Member Function
template<typename ReturnType, typename Class, typename... Args>
inline HleFunction *
makeFunction(ReturnType (Class::*fptr)(Args...), void *hostPtr = nullptr)
{
   auto func = new kernel::functions::HleMemberFunctionImpl<ReturnType, Class, Args...>();
   func->valid = true;
   func->wrapped_function = fptr;
   func->hostPtr = hostPtr;
   return func;
}

// Constructor Args
template<typename Class, typename... Args>
inline HleFunction *
makeConstructor()
{
   auto func = new kernel::functions::HleConstructorFunctionImpl<Class, Args...>();
   func->valid = true;
   return func;
}

// Destructor
template<typename Class>
inline HleFunction *
makeDestructor()
{
   auto func = new kernel::functions::HleDestructorFunctionImpl<Class>();
   func->valid = true;
   return func;
}

}  // namespace kernel
