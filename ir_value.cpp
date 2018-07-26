#include "ir_value.h"

#include "ir_value_char.inl"
#include "ir_value_string.inl"
#include "ir_value_numeric.inl"

#include "ir_value_fp_assembler.inl"
#include "ir_value_bcd_assembler.inl"
#include "ir_value_integer_assembler.inl"

#include "register_config.h"

#include <stdexcept>

using namespace std;

using TIRS8Value     = TIRNumericValue<TIRIntegerValueAssembler<int8_t>>;
using TIRU8Value     = TIRNumericValue<TIRIntegerValueAssembler<uint8_t>>;
using TIRS16Value    = TIRNumericValue<TIRIntegerValueAssembler<int16_t>>;
using TIRU16Value    = TIRNumericValue<TIRIntegerValueAssembler<uint16_t>>;
using TIRS24Value    = TIRNumericValue<TIRS24ValueAssembler>;
using TIRU24Value    = TIRNumericValue<TIRIntegerValueAssembler<uint32_t, 3>>;
using TIRS32Value    = TIRNumericValue<TIRIntegerValueAssembler<int32_t>>;
using TIRU32Value    = TIRNumericValue<TIRIntegerValueAssembler<uint32_t>>;
using TIRS64Value    = TIRNumericValue<TIRIntegerValueAssembler<int64_t>>;
using TIRU64Value    = TIRNumericValue<TIRIntegerValueAssembler<uint64_t>>;
using TIRBCD8Value   = TIRNumericValue<TIRBCDValueAssembler<uint8_t>>;
using TIRBCD16Value  = TIRNumericValue<TIRBCDValueAssembler<uint16_t>>;
using TIRBCD24Value  = TIRNumericValue<TIRBCDValueAssembler<uint32_t, false, 3>>;
using TIRBCD32Value  = TIRNumericValue<TIRBCDValueAssembler<uint32_t>>;
using TIRRBCD8Value  = TIRNumericValue<TIRBCDValueAssembler<uint8_t, true>>;
using TIRRBCD16Value = TIRNumericValue<TIRBCDValueAssembler<uint16_t, true>>;
using TIRRBCD24Value = TIRNumericValue<TIRBCDValueAssembler<uint32_t, true, 3>>;
using TIRRBCD32Value = TIRNumericValue<TIRBCDValueAssembler<uint32_t, true>>;
using TIRFloatValue  = TIRNumericValue<TIRFloatValueAssembler>;
using TIRDoubleValue = TIRNumericValue<TIRDoubleValueAssembler>;

using TIRStringValue  = TIRGenericByteStringValue<char>;
using TIRWStringValue = TIRGenericByteStringValue<char16_t>;

void TIRValue::ResetChanged()
{
    Changed = false;
}

bool TIRValue::IsChanged() const
{
    return Changed;
}

PIRValue TIRValue::Make(const TRegisterConfig & config)
{
    switch(config.Format) {
        case U8: return MakeUnique<TIRU8Value>();
        case S8: return MakeUnique<TIRS8Value>();
        case U16: return MakeUnique<TIRU16Value>();
        case S16: return MakeUnique<TIRS16Value>();
        case S24: return MakeUnique<TIRS24Value>();
        case U24: return MakeUnique<TIRU24Value>();
        case U32: return MakeUnique<TIRU32Value>();
        case S32: return MakeUnique<TIRS32Value>();
        case S64: return MakeUnique<TIRS64Value>();
        case U64: return MakeUnique<TIRU64Value>();
        case BCD8: return MakeUnique<TIRBCD8Value>();
        case BCD16: return MakeUnique<TIRBCD16Value>();
        case BCD24: return MakeUnique<TIRBCD24Value>();
        case BCD32: return MakeUnique<TIRBCD32Value>();
        case RBCD8: return MakeUnique<TIRRBCD8Value>();
        case RBCD16: return MakeUnique<TIRRBCD16Value>();
        case RBCD24: return MakeUnique<TIRRBCD24Value>();
        case RBCD32: return MakeUnique<TIRRBCD32Value>();
        case Float: return MakeUnique<TIRFloatValue>();
        case Double: return MakeUnique<TIRDoubleValue>();
        case Char8: return MakeUnique<TIRChar8Value>();
        case String: return MakeUnique<TIRStringValue>(config);
        case WString: return MakeUnique<TIRWStringValue>(config);
        default:
            throw runtime_error(
                string("format '") + RegisterFormatName(config.Format) +
                "' is not implemented"
            );
    }
}
