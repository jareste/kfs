#ifndef KPANIC_H
#define KPANIC_H

void __kpanic(char* msg, int fatal, const char* file, int line, const char* func_name);
#define kpanic(msg, fatal) __kpanic(msg, fatal, __FILE__, __LINE__, __func__)

#define ASSERT(x)   if (!(x)) { kpanic("Assertion failed: " #x, 1); }
#define NEVER_HERE  kpanic("NEVER_HERE", 1)

#endif /* KPANIC_H */
