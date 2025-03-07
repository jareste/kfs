#ifndef STDINT_H
#define STDINT_H

/* Exact-width integer types */
typedef signed char int8_t;
typedef short int int16_t;
typedef int int32_t;
typedef long long int int64_t;

typedef unsigned char uint8_t;
typedef unsigned short int uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long int uint64_t;

/* Minimum-width integer types */
typedef signed char int_least8_t;
typedef short int int_least16_t;
typedef int int_least32_t;
typedef long long int int_least64_t;

typedef unsigned char uint_least8_t;
typedef unsigned short int uint_least16_t;
typedef unsigned int uint_least32_t;
typedef unsigned long long int uint_least64_t;

/* Fastest minimum-width integer types */
typedef signed char int_fast8_t;
typedef int int_fast16_t;
typedef int int_fast32_t;
typedef long long int int_fast64_t;

typedef unsigned char uint_fast8_t;
typedef unsigned int uint_fast16_t;
typedef unsigned int uint_fast32_t;
typedef unsigned long long int uint_fast64_t;

/* Integer types capable of holding object pointers */
typedef int intptr_t;
typedef unsigned int uintptr_t;

/* Greatest-width integer types */
typedef long long int intmax_t;
typedef unsigned long long int uintmax_t;

typedef unsigned long size_t;
typedef long ssize_t;

/* uid gid euid */
typedef unsigned int uid_t;
typedef unsigned int gid_t;

/* Time type */
typedef unsigned long time_t;

/* Limits of exact-width integer types */
#define INT8_MIN (-128)
#define INT16_MIN (-32768)
#define INT32_MIN (-2147483648)
#define INT64_MIN (-9223372036854775807LL - 1)

#define INT8_MAX 127
#define INT16_MAX 32767
#define INT32_MAX 2147483647
#define INT64_MAX 9223372036854775807LL

#define UINT8_MAX 255
#define UINT16_MAX 65535
#define UINT32_MAX 4294967295U
#define UINT64_MAX 18446744073709551615ULL


#define LOW_16(address) (uint16_t)((address) & 0xFFFF)
#define HIGH_16(address) (uint16_t)(((address) >> 16) & 0xFFFF)
#define LOW_8(address) (uint8_t)((address) & 0xFF)
#define HIGH_8(address) (uint8_t)(((address) >> 8) & 0xFF)

#endif /* STDINT_H */
