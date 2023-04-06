#ifndef COMMON_DEF_H_
#define COMMON_DEF_H_

#include <stdlib.h>
#include <limits.h>
#include <stdint.h>

typedef int8_t   i8;
typedef int16_t  i16;
typedef int32_t  i32;
typedef int64_t  i64;
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef float    f32;
typedef double   f64;

#define ARR_LEN(arr) (sizeof(arr) / sizeof((arr)[0]))
#define UNUSED(var)  ((void)(var))

#endif /* COMMON_DEF_H_ */
