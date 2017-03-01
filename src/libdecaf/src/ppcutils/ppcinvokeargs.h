#pragma once
#include "ppctypeconv.h"

#include <common/be_val.h>
#include <libcpu/mem.h>
#include <type_traits>

namespace ppctypes
{

template <PpcType PpcTypeId, typename Type>
struct arg_converter_t;

inline uint32_t
getNextGPR(cpu::Core *state, size_t &r)
{
   uint32_t value;

   if (r > 10) {
      // Need to skip the backchain from the caller (8 bytes), plus the backchain we
      //  precreate as part of our kcstub (8 bytes).  Args come after those.
      auto addr = state->gpr[1] + 8 + 8 + 4 * static_cast<uint32_t>(r - 11);
      value = *mem::translate<be_val<uint32_t>>(addr);
   } else {
      value = state->gpr[r];
   }

   ++r;
   return value;
}

inline void
setNextGPR(cpu::Core *state, size_t &r, uint32_t value)
{
   if (r > 10) {
      auto addr = state->gpr[1] + 8 + 8 + 4 * static_cast<uint32_t>(r - 11);
      *mem::translate<be_val<uint32_t>>(addr) = value;
   } else {
      state->gpr[r] = value;
   }

   ++r;
}

template<typename Type>
struct arg_converter_t<PpcType::WORD, Type>
{
   static inline Type get(cpu::Core *state, size_t &r, size_t &f)
   {
      return ppctype_converter_t<Type>::from_ppc(getNextGPR(state, r));
   }

   static inline void set(cpu::Core *state, size_t &r, size_t &f, Type v)
   {
      uint32_t x;
      ppctype_converter_t<Type>::to_ppc(v, x);
      setNextGPR(state, r, x);
   }
};

template<typename Type>
struct arg_converter_t<PpcType::DWORD, Type>
{
   static inline Type get(cpu::Core *state, size_t &r, size_t &f)
   {
      auto x = getNextGPR(state, r);
      auto y = getNextGPR(state, r);
      return ppctype_converter_t<Type>::from_ppc(x, y);
   }

   static inline void set(cpu::Core *state, size_t &r, size_t &f, Type v)
   {
      uint32_t x, y;
      ppctype_converter_t<Type>::to_ppc(v, x, y);
      setNextGPR(state, r, x);
      setNextGPR(state, r, y);
   }
};

template<typename Type>
struct arg_converter_t<PpcType::FLOAT, Type>
{
   static inline Type get(cpu::Core *state, size_t &r, size_t &f)
   {
      auto& x = state->fpr[f++].paired0;
      return ppctype_converter_t<Type>::from_ppc(x);
   }

   static inline void set(cpu::Core *state, size_t &r, size_t &f, Type v)
   {
      auto& x = state->fpr[f++].paired0;
      ppctype_converter_t<Type>::to_ppc(v, x);
   }
};

template<typename Type>
struct arg_converter_t<PpcType::DOUBLE, Type>
{
   static inline Type get(cpu::Core *state, size_t &r, size_t &f)
   {
      auto& x = state->fpr[f++].paired0;
      return ppctype_converter_t<Type>::from_ppc(x);
   }

   static inline void set(cpu::Core *state, size_t &r, size_t &f, Type v)
   {
      auto& x = state->fpr[f++].paired0;
      ppctype_converter_t<Type>::to_ppc(v, x);
   }
};

static constexpr inline size_t
alignRegister64(size_t r)
{
   return ((r % 2) == 0) ? (r + 1) : r;
}

template<typename Type>
inline void
setArgument(cpu::Core *state, size_t &r, size_t &f, Type v)
{
   if (ppctype_converter_t<Type>::ppc_type == PpcType::DWORD) {
      r = alignRegister64(r);
   }

   return arg_converter_t<ppctype_converter_t<Type>::ppc_type, Type>::set(state, r, f, v);
}

// Grab an argument from registers for function
template<typename Type>
inline Type
getArgument(cpu::Core *state, size_t &r, size_t &f)
{
   if (ppctype_converter_t<Type>::ppc_type == PpcType::DWORD) {
      r = alignRegister64(r);
   }

   return arg_converter_t<ppctype_converter_t<Type>::ppc_type, Type>::get(state, r, f);
}

} // namespace ppctypes
