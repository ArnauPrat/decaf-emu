#include <cfenv>
#include <cmath>
#include <numeric>
#include "../cpu_internal.h"
#include "interpreter_insreg.h"
#include "interpreter.h"
#include "common/bitutils.h"
#include "common/floatutils.h"

using espresso::FPSCRRegisterBits;
using espresso::FloatingPointResultFlags;
using espresso::FloatingPointRoundMode;

const int fres_expected_base[] =
{
   0x7ff800, 0x783800, 0x70ea00, 0x6a0800,
   0x638800, 0x5d6200, 0x579000, 0x520800,
   0x4cc800, 0x47ca00, 0x430800, 0x3e8000,
   0x3a2c00, 0x360800, 0x321400, 0x2e4a00,
   0x2aa800, 0x272c00, 0x23d600, 0x209e00,
   0x1d8800, 0x1a9000, 0x17ae00, 0x14f800,
   0x124400, 0x0fbe00, 0x0d3800, 0x0ade00,
   0x088400, 0x065000, 0x041c00, 0x020c00,
};

const int fres_expected_dec[] =
{
   0x3e1, 0x3a7, 0x371, 0x340,
   0x313, 0x2ea, 0x2c4, 0x2a0,
   0x27f, 0x261, 0x245, 0x22a,
   0x212, 0x1fb, 0x1e5, 0x1d1,
   0x1be, 0x1ac, 0x19b, 0x18b,
   0x17c, 0x16e, 0x15b, 0x15b,
   0x143, 0x143, 0x12d, 0x12d,
   0x11a, 0x11a, 0x108, 0x106,
};

double
ppc_estimate_reciprocal(double v)
{
   auto bits = get_float_bits(v);

   if (bits.mantissa == 0 && bits.exponent == 0) {
      return std::copysign(std::numeric_limits<double>::infinity(), v);
   }

   if (bits.exponent == bits.exponent_max) {
      if (bits.mantissa == 0) {
         return std::copysign(0.0, v);
      }
      return static_cast<float>(v);
   }

   if (bits.exponent < 895) {
      std::feraiseexcept(FE_OVERFLOW | FE_INEXACT);
      return std::copysign(std::numeric_limits<float>::max(), v);
   }

   if (bits.exponent > 1150) {
      std::feraiseexcept(FE_UNDERFLOW | FE_INEXACT);
      return std::copysign(0.0, v);
   }

   int idx = (int)(bits.mantissa >> 37);
   bits.exponent = 0x7FD - bits.exponent;
   bits.mantissa = (int64_t)(fres_expected_base[idx / 1024] - (fres_expected_dec[idx / 1024] * (idx % 1024) + 1) / 2) << 29;
   return bits.v;
}

void
updateFEX_VX(cpu::Core *state)
{
   auto &fpscr = state->fpscr;

   // Invalid Operation Summary
   fpscr.vx =
        fpscr.vxsnan
      | fpscr.vxisi
      | fpscr.vxidi
      | fpscr.vxzdz
      | fpscr.vximz
      | fpscr.vxvc
      | fpscr.vxsqrt
      | fpscr.vxsoft
      | fpscr.vxcvi;

   // FP Enabled Exception Summary
   fpscr.fex =
        (fpscr.vx & fpscr.ve)
      | (fpscr.ox & fpscr.oe)
      | (fpscr.ux & fpscr.ue)
      | (fpscr.zx & fpscr.ze)
      | (fpscr.xx & fpscr.xe);
}


void
updateFX_FEX_VX(cpu::Core *state, uint32_t oldValue)
{
   auto &fpscr = state->fpscr;

   updateFEX_VX(state);

   // FP Exception Summary
   const uint32_t newBits = (oldValue ^ fpscr.value) & fpscr.value;
   if (newBits & FPSCRRegisterBits::AllExceptions) {
      fpscr.fx = 1;
   }
}

void
updateFPSCR(cpu::Core *state, uint32_t oldValue)
{
   auto except = std::fetestexcept(FE_ALL_EXCEPT);
   auto round = std::fegetround();
   auto &fpscr = state->fpscr;

   // Underflow
   fpscr.ux |= !!(except & FE_UNDERFLOW);

   // Overflow
   fpscr.ox |= !!(except & FE_OVERFLOW);

   // Zerodivide
   fpscr.zx |= !!(except & FE_DIVBYZERO);

   // Inexact
   fpscr.fi = !!(except & FE_INEXACT);
   fpscr.xx |= fpscr.fi;

   // Fraction Rounded
   fpscr.fr = !!(round & FE_UPWARD);

   updateFX_FEX_VX(state, oldValue);

   std::feclearexcept(FE_ALL_EXCEPT);
}

template<typename Type>
void
updateFPRF(cpu::Core *state, Type value)
{
   auto cls = std::fpclassify(value);
   auto flags = 0u;

   if (cls == FP_NAN) {
      flags |= FloatingPointResultFlags::ClassDescriptor;
      flags |= FloatingPointResultFlags::Unordered;
   } else if (value != 0) {
      if (value > 0) {
         flags |= FloatingPointResultFlags::Positive;
      } else {
         flags |= FloatingPointResultFlags::Negative;
      }
      if (cls == FP_INFINITE) {
         flags |= FloatingPointResultFlags::Unordered;
      } else if (cls == FP_SUBNORMAL) {
         flags |= FloatingPointResultFlags::ClassDescriptor;
      }
   } else {
      flags |= FloatingPointResultFlags::Equal;
      if (std::signbit(value)) {
         flags |= FloatingPointResultFlags::ClassDescriptor;
      }
   }

   state->fpscr.fprf = flags;
}

// Make sure both float and double versions are available to other sources:
template void updateFPRF(cpu::Core *state, float value);
template void updateFPRF(cpu::Core *state, double value);

void
updateFloatConditionRegister(cpu::Core *state)
{
   state->cr.cr1 = state->fpscr.cr1;
}

// Helper for fmuls/fmadds to round the second (frC) operand appropriately.
// May also need to modify the first operand, so both operands are passed
// by reference.
void
roundForMultiply(double *a, double *c)
{
   // The mantissa is truncated from 52 to 24 bits, so bit 27 (counting from
   // the LSB) is rounded.
   const uint64_t roundBit = UINT64_C(1) << 27;

   FloatBitsDouble aBits = get_float_bits(*a);
   FloatBitsDouble cBits = get_float_bits(*c);

   // If the second operand has no bits that would be rounded, this whole
   // function is a no-op, so skip out early.
   if (!(cBits.uv & ((roundBit << 1) - 1))) {
      return;
   }

   // If the first operand is zero, the result is always zero (even if the
   // second operand would round to infinity), so avoid generating any
   // exceptions.
   if (is_zero(*a)) {
      return;
   }

   // If the first operand is infinity and the second is not zero, the result
   // is always infinity; get out now so we don't have to worry about it in
   // normalization.
   if (is_infinity(*a)) {
      return;
   }

   // If the second operand is a denormal, we normalize it before rounding,
   // adjusting the exponent of the other operand accordingly.  If the
   // other operand becomes denormal, the product will round to zero in any
   // case, so we just abort and let the operation proceed normally.
   if (is_denormal(*c)) {
      auto cSign = cBits.sign;
      while (cBits.exponent == 0) {
         cBits.uv <<= 1;
         if (aBits.exponent == 0) {
            return;
         }
         aBits.exponent--;
      }
      cBits.sign = cSign;
   }

   // Perform the rounding.  If this causes the value to go to infinity,
   // we move a power of two to the other operand (if possible) for the
   // case of an FMA operation in which we need to keep precision for the
   // intermediate result.  Note that this particular rounding operation
   // ignores FPSCR[RN].
   cBits.uv &= -static_cast<int64_t>(roundBit);
   cBits.uv += cBits.uv & roundBit;
   if (is_infinity(cBits.v)) {
      cBits.exponent--;
      if (aBits.exponent == 0) {
         auto aSign = aBits.sign;
         aBits.uv <<= 1;
         aBits.sign = aSign;
      } else if (aBits.exponent < aBits.exponent_max - 1) {
         aBits.exponent++;
      } else {
         // The product will overflow anyway, so just leave the first
         // operand alone and let the host FPU raise exceptions as
         // appropriate.
      }
   }

   *a = aBits.v;
   *c = cBits.v;
}

// Floating Arithmetic
enum FPArithOperator {
    FPAdd,
    FPSub,
    FPMul,
    FPDiv,
};
template<FPArithOperator op, typename Type>
static void
fpArithGeneric(cpu::Core *state, Instruction instr)
{
   double a, b;
   Type d;

   a = state->fpr[instr.frA].value;
   b = state->fpr[op == FPMul ? instr.frC : instr.frB].value;

   const bool vxsnan = is_signalling_nan(a) || is_signalling_nan(b);
   bool vxisi, vximz, vxidi, vxzdz, zx;

   switch (op) {
   case FPAdd:
      vxisi = is_infinity(a) && is_infinity(b) && std::signbit(a) != std::signbit(b);
      vximz = false;
      vxidi = false;
      vxzdz = false;
      zx = false;
      break;
   case FPSub:
      vxisi = is_infinity(a) && is_infinity(b) && std::signbit(a) == std::signbit(b);
      vximz = false;
      vxidi = false;
      vxzdz = false;
      zx = false;
      break;
   case FPMul:
      vxisi = false;
      vximz = (is_infinity(a) && is_zero(b)) || (is_zero(a) && is_infinity(b));
      vxidi = false;
      vxzdz = false;
      zx = false;
      break;
   case FPDiv:
      vxisi = false;
      vximz = false;
      vxidi = is_infinity(a) && is_infinity(b);
      vxzdz = is_zero(a) && is_zero(b);
      zx = !(vxzdz || vxsnan) && is_zero(b);
      break;
   }

   const uint32_t oldFPSCR = state->fpscr.value;
   state->fpscr.vxsnan |= vxsnan;
   state->fpscr.vxisi |= vxisi;
   state->fpscr.vximz |= vximz;
   state->fpscr.vxidi |= vxidi;
   state->fpscr.vxzdz |= vxzdz;

   if ((vxsnan || vxisi || vximz || vxidi || vxzdz) && state->fpscr.ve) {
      updateFX_FEX_VX(state, oldFPSCR);
   } else if (zx && state->fpscr.ze) {
      state->fpscr.zx = 1;
      updateFX_FEX_VX(state, oldFPSCR);
   } else {
      if (is_nan(a)) {
         d = static_cast<Type>(make_quiet(a));
      } else if (is_nan(b)) {
         d = static_cast<Type>(make_quiet(b));
      } else if (vxisi || vximz || vxidi || vxzdz) {
         d = make_nan<Type>();
      } else {
         // The Espresso appears to use double precision arithmetic even for
         // single-precision instructions (for example, 2^128 * 0.5 does not
         // cause overflow), so we do the same here.
         switch (op) {
         case FPAdd:
            d = static_cast<Type>(a + b);
            break;
         case FPSub:
            d = static_cast<Type>(a - b);
            break;
         case FPMul:
            // But!  The second operand to a single-precision multiply
            // operation is rounded to 24 bits.
            if (std::is_same<Type, float>::value) {
               roundForMultiply(&a, &b);
            }
            d = static_cast<Type>(a * b);
            break;
         case FPDiv:
            d = static_cast<Type>(a / b);
            break;
         }
      }

      if (std::is_same<Type, float>::value) {
         state->fpr[instr.frD].paired0 = extend_float(static_cast<float>(d));
         state->fpr[instr.frD].paired1 = extend_float(static_cast<float>(d));
      } else {
         state->fpr[instr.frD].value = d;
      }

      updateFPRF(state, d);
      updateFPSCR(state, oldFPSCR);
   }

   if (instr.rc) {
      updateFloatConditionRegister(state);
   }
}

// Floating Add Double
static void
fadd(cpu::Core *state, Instruction instr)
{
   fpArithGeneric<FPAdd, double>(state, instr);
}

// Floating Add Single
static void
fadds(cpu::Core *state, Instruction instr)
{
   fpArithGeneric<FPAdd, float>(state, instr);
}

// Floating Divide Double
static void
fdiv(cpu::Core *state, Instruction instr)
{
   fpArithGeneric<FPDiv, double>(state, instr);
}

// Floating Divide Single
static void
fdivs(cpu::Core *state, Instruction instr)
{
   fpArithGeneric<FPDiv, float>(state, instr);
}

// Floating Multiply Double
static void
fmul(cpu::Core *state, Instruction instr)
{
   fpArithGeneric<FPMul, double>(state, instr);
}

// Floating Multiply Single
static void
fmuls(cpu::Core *state, Instruction instr)
{
   fpArithGeneric<FPMul, float>(state, instr);
}

// Floating Subtract Double
static void
fsub(cpu::Core *state, Instruction instr)
{
   fpArithGeneric<FPSub, double>(state, instr);
}

// Floating Subtract Single
static void
fsubs(cpu::Core *state, Instruction instr)
{
   fpArithGeneric<FPSub, float>(state, instr);
}

// Floating Reciprocal Estimate Single
static void
fres(cpu::Core *state, Instruction instr)
{
   double b;
   float d;
   b = state->fpr[instr.frB].value;

   const bool vxsnan = is_signalling_nan(b);
   const bool zx = is_zero(b);

   const uint32_t oldFPSCR = state->fpscr.value;
   state->fpscr.vxsnan |= vxsnan;

   if (vxsnan && state->fpscr.ve) {
      updateFX_FEX_VX(state, oldFPSCR);
   } else if (zx && state->fpscr.ze) {
      state->fpscr.zx = 1;
      updateFX_FEX_VX(state, oldFPSCR);
   } else {
      d = static_cast<float>(ppc_estimate_reciprocal(b));
      state->fpr[instr.frD].paired0 = d;
      state->fpr[instr.frD].paired1 = d;
      updateFPRF(state, d);
      state->fpscr.zx |= zx;
      if (std::fetestexcept(FE_INEXACT)) {
         // On inexact result, fres sets FPSCR[FI] without also setting
         // FPSCR[XX].
         std::feclearexcept(FE_INEXACT);
         updateFPSCR(state, oldFPSCR);
         state->fpscr.fi = 1;
      } else {
         updateFPSCR(state, oldFPSCR);
      }
   }

   if (instr.rc) {
      updateFloatConditionRegister(state);
   }
}

// Floating Reciprocal Square Root Estimate
static void
frsqrte(cpu::Core *state, Instruction instr)
{
   double b, d;
   b = state->fpr[instr.frB].value;

   const bool vxsnan = is_signalling_nan(b);
   const bool vxsqrt = !vxsnan && std::signbit(b) && !is_zero(b);
   const bool zx = is_zero(b);

   const uint32_t oldFPSCR = state->fpscr.value;
   state->fpscr.vxsnan |= vxsnan;
   state->fpscr.vxsqrt |= vxsqrt;

   if ((vxsnan || vxsqrt) && state->fpscr.ve) {
      updateFX_FEX_VX(state, oldFPSCR);
   } else if (zx && state->fpscr.ze) {
      state->fpscr.zx = 1;
      updateFX_FEX_VX(state, oldFPSCR);
   } else {
      if (vxsqrt) {
         d = make_nan<double>();
      } else {
         d = 1.0 / std::sqrt(b);
      }
      state->fpr[instr.frD].value = d;
      updateFPRF(state, d);
      state->fpscr.zx |= zx;
      updateFPSCR(state, oldFPSCR);
   }

   if (instr.rc) {
      updateFloatConditionRegister(state);
   }
}

static void
fsel(cpu::Core *state, Instruction instr)
{
   double a, b, c, d;
   a = state->fpr[instr.frA].value;
   b = state->fpr[instr.frB].value;
   c = state->fpr[instr.frC].value;

   if (a >= 0.0) {
      d = c;
   } else {
      d = b;
   }

   state->fpr[instr.frD].value = d;

   if (instr.rc) {
      updateFloatConditionRegister(state);
   }
}

// Fused multiply-add instructions
enum FMAFlags
{
   FMASubtract   = 1 << 0, // Subtract instead of add
   FMANegate     = 1 << 1, // Negate result
   FMASinglePrec = 1 << 2, // Round result to single precision
};

template<unsigned flags>
static void
fmaGeneric(cpu::Core *state, Instruction instr)
{
   double a, b, c, d;
   a = state->fpr[instr.frA].value;
   b = state->fpr[instr.frB].value;
   c = state->fpr[instr.frC].value;

   const double addend = (flags & FMASubtract) ? -b : b;

   const bool vxsnan = is_signalling_nan(a) || is_signalling_nan(b) || is_signalling_nan(c);
   const bool vximz = (is_infinity(a) && is_zero(c)) || (is_zero(a) && is_infinity(c));
   const bool vxisi = (!vximz && !is_nan(a) && !is_nan(c)
                       && (is_infinity(a) || is_infinity(c)) && is_infinity(b)
                       && (std::signbit(a) ^ std::signbit(c)) != std::signbit(addend));

   const uint32_t oldFPSCR = state->fpscr.value;
   state->fpscr.vxsnan |= vxsnan;
   state->fpscr.vxisi |= vxisi;
   state->fpscr.vximz |= vximz;

   if ((vxsnan || vxisi || vximz) && state->fpscr.ve) {
      updateFX_FEX_VX(state, oldFPSCR);
   } else {
      if (is_nan(a)) {
         d = make_quiet(a);
      } else if (is_nan(b)) {
         d = make_quiet(b);
      } else if (is_nan(c)) {
         d = make_quiet(c);
      } else if (vxisi || vximz) {
         d = make_nan<double>();
      } else {
         if (flags & FMASinglePrec) {
            roundForMultiply(&a, &c);
         }

         d = std::fma(a, c, addend);

         if (flags & FMANegate) {
            d = -d;
         }
      }

      if (flags & FMASinglePrec) {
         d = extend_float(static_cast<float>(d));
         state->fpr[instr.frD].paired0 = d;
         state->fpr[instr.frD].paired1 = d;
      } else {
         state->fpr[instr.frD].value = d;
         // Note that Intel CPUs report underflow based on the value _after_
         // rounding, while the Espresso reports underflow _before_ rounding.
         // (IEEE 754 allows an implementer to choose whether to report
         // underflow before or after rounding, so both of these behaviors
         // are technically compliant.)  Because of this, if an unrounded
         // FMA result is slightly less in magnitude than the minimum normal
         // value but is rounded to that value, the emulated FPSCR state will
         // differ from a real Espresso in that the UX bit will not be set.
      }

      updateFPRF(state, d);
      updateFPSCR(state, oldFPSCR);
   }

   if (instr.rc) {
      updateFloatConditionRegister(state);
   }
}

// Floating Multiply-Add
static void
fmadd(cpu::Core *state, Instruction instr)
{
   return fmaGeneric<0>(state, instr);
}

// Floating Multiply-Add Single
static void
fmadds(cpu::Core *state, Instruction instr)
{
   return fmaGeneric<FMASinglePrec>(state, instr);
}

// Floating Multiply-Sub
static void
fmsub(cpu::Core *state, Instruction instr)
{
   return fmaGeneric<FMASubtract>(state, instr);
}

// Floating Multiply-Sub Single
static void
fmsubs(cpu::Core *state, Instruction instr)
{
   return fmaGeneric<FMASubtract | FMASinglePrec>(state, instr);
}

// Floating Negative Multiply-Add
static void
fnmadd(cpu::Core *state, Instruction instr)
{
   return fmaGeneric<FMANegate>(state, instr);
}

// Floating Negative Multiply-Add Single
static void
fnmadds(cpu::Core *state, Instruction instr)
{
   return fmaGeneric<FMANegate | FMASinglePrec>(state, instr);
}

// Floating Negative Multiply-Sub
static void
fnmsub(cpu::Core *state, Instruction instr)
{
   return fmaGeneric<FMANegate | FMASubtract>(state, instr);
}

// Floating Negative Multiply-Sub Single
static void
fnmsubs(cpu::Core *state, Instruction instr)
{
   return fmaGeneric<FMANegate | FMASubtract | FMASinglePrec>(state, instr);
}

// fctiw/fctiwz common implementation
static void
fctiwGeneric(cpu::Core *state, Instruction instr, FloatingPointRoundMode roundMode)
{
   double b;
   int32_t bi;
   b = state->fpr[instr.frB].value;

   const bool vxsnan = is_signalling_nan(b);
   bool vxcvi, fi;

   if (is_nan(b)) {
      vxcvi = true;
      fi = false;
      bi = INT_MIN;
   } else if (b > static_cast<double>(INT_MAX)) {
      vxcvi = true;
      fi = false;
      bi = INT_MAX;
   } else if (b < static_cast<double>(INT_MIN)) {
      vxcvi = true;
      fi = false;
      bi = INT_MIN;
   } else {
      vxcvi = false;
      switch (roundMode) {
      case FloatingPointRoundMode::Nearest:
         // We have to use nearbyint() instead of round() here, because
         // round() rounds 0.5 away from zero instead of to the nearest
         // even integer.  nearbyint() is dependent on the host's FPU
         // rounding mode, but since that will reflect FPSCR here, it's
         // safe to use.
         bi = static_cast<int32_t>(std::nearbyint(b));
         break;
      case FloatingPointRoundMode::Zero:
         bi = static_cast<int32_t>(std::trunc(b));
         break;
      case FloatingPointRoundMode::Positive:
         bi = static_cast<int32_t>(std::ceil(b));
         break;
      case FloatingPointRoundMode::Negative:
         bi = static_cast<int32_t>(std::floor(b));
         break;
      }
      fi = get_float_bits(b).exponent < 1075 && bi != b;
   }

   const uint32_t oldFPSCR = state->fpscr.value;
   state->fpscr.vxsnan |= vxsnan;
   state->fpscr.vxcvi |= vxcvi;

   if ((vxsnan || vxcvi) && state->fpscr.ve) {
      state->fpscr.fr = 0;
      state->fpscr.fi = 0;
      updateFX_FEX_VX(state, oldFPSCR);
   } else {
      state->fpr[instr.frD].iw1 = bi;
      state->fpr[instr.frD].iw0 = 0xFFF80000 | (is_negative_zero(b) ? 1 : 0);
      updateFPSCR(state, oldFPSCR);
      // We need to set FPSCR[FI] manually since the rounding functions
      // don't always raise inexact exceptions.
      if (fi) {
         state->fpscr.fi = 1;
         state->fpscr.xx = 1;
         updateFX_FEX_VX(state, oldFPSCR);
      }
   }

   if (instr.rc) {
      updateFloatConditionRegister(state);
   }
}

static void
fctiw(cpu::Core *state, Instruction instr)
{
   return fctiwGeneric(state, instr, static_cast<FloatingPointRoundMode>(state->fpscr.rn));
}

// Floating Convert to Integer Word with Round toward Zero
static void
fctiwz(cpu::Core *state, Instruction instr)
{
   return fctiwGeneric(state, instr, FloatingPointRoundMode::Zero);
}

// Floating Round to Single
static void
frsp(cpu::Core *state, Instruction instr)
{
   auto b = state->fpr[instr.frB].value;
   auto vxsnan = is_signalling_nan(b);

   const uint32_t oldFPSCR = state->fpscr.value;
   state->fpscr.vxsnan |= vxsnan;

   if (vxsnan && state->fpscr.ve) {
      updateFX_FEX_VX(state, oldFPSCR);
   } else {
      auto d = static_cast<float>(b);
      state->fpr[instr.frD].paired0 = d;
      // frD(ps1) is left undefined in the 750CL manual, but the processor
      // actually copies the result to ps1 like other single-precision
      // instructions.
      state->fpr[instr.frD].paired1 = d;
      updateFPRF(state, d);
      updateFPSCR(state, oldFPSCR);
   }

   if (instr.rc) {
      updateFloatConditionRegister(state);
   }
}

// TODO: do fabs/fnabs/fneg behave like fmr w.r.t. paired singles?
// Floating Absolute Value
static void
fabs(cpu::Core *state, Instruction instr)
{
   uint64_t b, d;

   b = state->fpr[instr.frB].idw;
   d = clear_bit(b, 63);
   state->fpr[instr.frD].idw = d;

   if (instr.rc) {
      updateFloatConditionRegister(state);
   }
}

// Floating Negative Absolute Value
static void
fnabs(cpu::Core *state, Instruction instr)
{
   uint64_t b, d;

   b = state->fpr[instr.frB].idw;
   d = set_bit(b, 63);
   state->fpr[instr.frD].idw = d;

   if (instr.rc) {
      updateFloatConditionRegister(state);
   }
}

// Floating Move Register
static void
fmr(cpu::Core *state, Instruction instr)
{
   state->fpr[instr.frD].idw = state->fpr[instr.frB].idw;

   if (instr.rc) {
      updateFloatConditionRegister(state);
   }
}

// Floating Negate
static void
fneg(cpu::Core *state, Instruction instr)
{
   uint64_t b, d;

   b = state->fpr[instr.frB].idw;
   d = flip_bit(b, 63);
   state->fpr[instr.frD].idw = d;

   if (instr.rc) {
      updateFloatConditionRegister(state);
   }
}

// Move from FPSCR
static void
mffs(cpu::Core *state, Instruction instr)
{
   state->fpr[instr.frD].iw1 = state->fpscr.value;

   if (instr.rc) {
      updateFloatConditionRegister(state);
   }
}

// Move to FPSCR Bit 0
static void
mtfsb0(cpu::Core *state, Instruction instr)
{
   state->fpscr.value = clear_bit(state->fpscr.value, 31 - instr.crbD);
   updateFEX_VX(state);
   if (instr.crbD >= 30) {
      cpu::this_core::updateRoundingMode();
   }

   if (instr.rc) {
      updateFloatConditionRegister(state);
   }
}

// Move to FPSCR Bit 1
static void
mtfsb1(cpu::Core *state, Instruction instr)
{
   const uint32_t oldValue = state->fpscr.value;
   state->fpscr.value = set_bit(state->fpscr.value, 31 - instr.crbD);
   updateFX_FEX_VX(state, oldValue);
   if (instr.crbD >= 30) {
      cpu::this_core::updateRoundingMode();
   }

   if (instr.rc) {
      updateFloatConditionRegister(state);
   }
}

// Move to FPSCR Fields
static void
mtfsf(cpu::Core *state, Instruction instr)
{
   const uint32_t value = state->fpr[instr.frB].iw1;
   for (int field = 0; field < 8; field++) {
      // Technically field 0 is at the high end, but as long as the bit
      // position in the mask and the field we operate on match up, it
      // doesn't matter which direction we go in.  So we use host bit
      // order for simplicity.
      if (get_bit(instr.fm, field)) {
         const uint32_t mask = make_bitmask(4 * field, 4 * field + 3);
         state->fpscr.value &= ~mask;
         state->fpscr.value |= value & mask;
      }
   }
   updateFEX_VX(state);
   if (get_bit(instr.fm, 0)) {
      cpu::this_core::updateRoundingMode();
   }

   if (instr.rc) {
      updateFloatConditionRegister(state);
   }
}

// Move to FPSCR Field Immediate
static void
mtfsfi(cpu::Core *state, Instruction instr)
{
   const int shift = 4 * (7 - instr.crfD);
   state->fpscr.value &= ~(0xF << shift);
   state->fpscr.value |= instr.imm << shift;
   updateFEX_VX(state);
   if (instr.crfD == 7) {
      cpu::this_core::updateRoundingMode();
   }

   if (instr.rc) {
      updateFloatConditionRegister(state);
   }
}

void
cpu::interpreter::registerFloatInstructions()
{
   RegisterInstruction(fadd);
   RegisterInstruction(fadds);
   RegisterInstruction(fdiv);
   RegisterInstruction(fdivs);
   RegisterInstruction(fmul);
   RegisterInstruction(fmuls);
   RegisterInstruction(fsub);
   RegisterInstruction(fsubs);
   RegisterInstruction(fres);
   RegisterInstruction(frsqrte);
   RegisterInstruction(fsel);
   RegisterInstruction(fmadd);
   RegisterInstruction(fmadds);
   RegisterInstruction(fmsub);
   RegisterInstruction(fmsubs);
   RegisterInstruction(fnmadd);
   RegisterInstruction(fnmadds);
   RegisterInstruction(fnmsub);
   RegisterInstruction(fnmsubs);
   RegisterInstruction(fctiw);
   RegisterInstruction(fctiwz);
   RegisterInstruction(frsp);
   RegisterInstruction(fabs);
   RegisterInstruction(fnabs);
   RegisterInstruction(fmr);
   RegisterInstruction(fneg);
   RegisterInstruction(mffs);
   RegisterInstruction(mtfsb0);
   RegisterInstruction(mtfsb1);
   RegisterInstruction(mtfsf);
   RegisterInstruction(mtfsfi);
}
