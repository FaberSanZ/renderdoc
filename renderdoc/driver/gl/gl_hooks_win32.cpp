/******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2015-2018 Baldur Karlsson
 * Copyright (c) 2014 Crytek
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 ******************************************************************************/

#include "common/threading.h"
#include "driver/gl/gl_common.h"
#include "driver/gl/gl_dispatch_table.h"
#include "driver/gl/gl_dispatch_table_defs.h"
#include "driver/gl/gl_driver.h"
#include "hooks/hooks.h"
#include "strings/string_utils.h"

#define HookInit(function)                                                       \
  CONCAT(function, _hook)                                                        \
      .Register("opengl32.dll", STRINGIZE(function), CONCAT(function, _hooked)); \
  GL.function = CONCAT(function, _hook)();

#define HookExtension(funcPtrType, function)         \
  if(!strcmp(func, STRINGIZE(function)))             \
  {                                                  \
    if(GL.function == NULL)                          \
      GL.function = (funcPtrType)realFunc;           \
    return (PROC)&glhooks.CONCAT(function, _hooked); \
  }

#define HookExtensionAlias(funcPtrType, function, alias) \
  if(!strcmp(func, STRINGIZE(alias)))                    \
  {                                                      \
    if(GL.function == NULL)                              \
      GL.function = (funcPtrType)realFunc;               \
    return (PROC)&glhooks.CONCAT(alias, _hooked);        \
  }

#if 0    // debug print for each unsupported function requested (but not used)
#define HandleUnsupported(funcPtrType, function)                                           \
  if(!strcmp(func, STRINGIZE(function)))                                                   \
  {                                                                                        \
    glhooks.CONCAT(unsupported_real_, function) = (CONCAT(function, _hooktype))realFunc;   \
    RDCDEBUG("Requesting function pointer for unsupported function " STRINGIZE(function)); \
    return (PROC)&glhooks.CONCAT(function, _hooked);                                       \
  }
#else
#define HandleUnsupported(funcPtrType, function)                                         \
  if(!strcmp(func, STRINGIZE(function)))                                                 \
  {                                                                                      \
    glhooks.CONCAT(unsupported_real_, function) = (CONCAT(function, _hooktype))realFunc; \
    return (PROC)&glhooks.CONCAT(function, _hooked);                                     \
  }
#endif

/*
  in bash:

    function WrapperMacro()
    {
        NAME=$1;
        N=$2;
        ALIAS=$3;
        if [ $ALIAS -eq 1 ]; then
          echo -n "#define $NAME$N(ret, function, realfunc";
        else
          echo -n "#define $NAME$N(ret, function";
        fi
            for I in `seq 1 $N`; do echo -n ", t$I, p$I"; done;
        echo ") \\";

        echo -en "  HookedFunction<ret(WINAPI *)(";
            for I in `seq 1 $N`; do echo -n "t$I"; if [ $I -ne $N ]; then echo -n ", "; fi; done;
        echo ")> CONCAT(function, _hook); \\";

        echo -en "  typedef ret(WINAPI *CONCAT(function, _hooktype)) (";
            for I in `seq 1 $N`; do echo -n "t$I"; if [ $I -ne $N ]; then echo -n ", "; fi; done;
        echo "); \\";

        echo -en "  static ret WINAPI CONCAT(function, _hooked)(";
            for I in `seq 1 $N`; do echo -n "t$I p$I"; if [ $I -ne $N ]; then echo -n ", "; fi;
  done;
        echo ") \\";

        echo -e "  { \\";
        echo -e "    SCOPED_LOCK(glLock); \\";
        echo -e '    if(!glhooks.m_HaveContextCreation) \\';
        if [ $ALIAS -eq 1 ]; then
          echo -n "    return GL.realfunc(";
        else
          echo -n "    return GL.function(";
        fi
            for I in `seq 1 $N`; do echo -n "p$I"; if [ $I -ne $N ]; then echo -n ", "; fi; done;
        echo -e "); \\";
        echo -e "    gl_CurChunk = GLChunk::function; \\";
        if [ $ALIAS -eq 1 ]; then
          echo -n "    return glhooks.GetDriver()->realfunc(";
        else
          echo -n "    return glhooks.GetDriver()->function(";
        fi
            for I in `seq 1 $N`; do echo -n "p$I"; if [ $I -ne $N ]; then echo -n ", "; fi; done;
        echo -e "); \\";
        echo -e "  }";
    }

  for I in `seq 0 17`; do WrapperMacro HookWrapper $I 0; echo; done
  for I in `seq 0 17`; do WrapperMacro HookAliasWrapper $I 1; echo; done

  */

// don't want these definitions, the only place we'll use these is as parameter/variable names
#ifdef near
#undef near
#endif

#ifdef far
#undef far
#endif

#define HookWrapper0(ret, function)                        \
  HookedFunction<ret(WINAPI *)()> CONCAT(function, _hook); \
  typedef ret(WINAPI *CONCAT(function, _hooktype))();      \
  static ret WINAPI CONCAT(function, _hooked)()            \
  {                                                        \
    SCOPED_LOCK(glLock);                                   \
    if(!glhooks.m_HaveContextCreation)                     \
      return GL.function();                                \
    gl_CurChunk = GLChunk::function;                       \
    return glhooks.GetDriver()->function();                \
  }

#define HookWrapper1(ret, function, t1, p1)                  \
  HookedFunction<ret(WINAPI *)(t1)> CONCAT(function, _hook); \
  typedef ret(WINAPI *CONCAT(function, _hooktype))(t1);      \
  static ret WINAPI CONCAT(function, _hooked)(t1 p1)         \
  {                                                          \
    SCOPED_LOCK(glLock);                                     \
    if(!glhooks.m_HaveContextCreation)                       \
      return GL.function(p1);                                \
    gl_CurChunk = GLChunk::function;                         \
    return glhooks.GetDriver()->function(p1);                \
  }

#define HookWrapper2(ret, function, t1, p1, t2, p2)              \
  HookedFunction<ret(WINAPI *)(t1, t2)> CONCAT(function, _hook); \
  typedef ret(WINAPI *CONCAT(function, _hooktype))(t1, t2);      \
  static ret WINAPI CONCAT(function, _hooked)(t1 p1, t2 p2)      \
  {                                                              \
    SCOPED_LOCK(glLock);                                         \
    if(!glhooks.m_HaveContextCreation)                           \
      return GL.function(p1, p2);                                \
    gl_CurChunk = GLChunk::function;                             \
    return glhooks.GetDriver()->function(p1, p2);                \
  }

#define HookWrapper3(ret, function, t1, p1, t2, p2, t3, p3)          \
  HookedFunction<ret(WINAPI *)(t1, t2, t3)> CONCAT(function, _hook); \
  typedef ret(WINAPI *CONCAT(function, _hooktype))(t1, t2, t3);      \
  static ret WINAPI CONCAT(function, _hooked)(t1 p1, t2 p2, t3 p3)   \
  {                                                                  \
    SCOPED_LOCK(glLock);                                             \
    if(!glhooks.m_HaveContextCreation)                               \
      return GL.function(p1, p2, p3);                                \
    gl_CurChunk = GLChunk::function;                                 \
    return glhooks.GetDriver()->function(p1, p2, p3);                \
  }

#define HookWrapper4(ret, function, t1, p1, t2, p2, t3, p3, t4, p4)       \
  HookedFunction<ret(WINAPI *)(t1, t2, t3, t4)> CONCAT(function, _hook);  \
  typedef ret(WINAPI *CONCAT(function, _hooktype))(t1, t2, t3, t4);       \
  static ret WINAPI CONCAT(function, _hooked)(t1 p1, t2 p2, t3 p3, t4 p4) \
  {                                                                       \
    SCOPED_LOCK(glLock);                                                  \
    if(!glhooks.m_HaveContextCreation)                                    \
      return GL.function(p1, p2, p3, p4);                                 \
    gl_CurChunk = GLChunk::function;                                      \
    return glhooks.GetDriver()->function(p1, p2, p3, p4);                 \
  }

#define HookWrapper5(ret, function, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5)      \
  HookedFunction<ret(WINAPI *)(t1, t2, t3, t4, t5)> CONCAT(function, _hook);     \
  typedef ret(WINAPI *CONCAT(function, _hooktype))(t1, t2, t3, t4, t5);          \
  static ret WINAPI CONCAT(function, _hooked)(t1 p1, t2 p2, t3 p3, t4 p4, t5 p5) \
  {                                                                              \
    SCOPED_LOCK(glLock);                                                         \
    if(!glhooks.m_HaveContextCreation)                                           \
      return GL.function(p1, p2, p3, p4, p5);                                    \
    gl_CurChunk = GLChunk::function;                                             \
    return glhooks.GetDriver()->function(p1, p2, p3, p4, p5);                    \
  }

#define HookWrapper6(ret, function, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6, p6)     \
  HookedFunction<ret(WINAPI *)(t1, t2, t3, t4, t5, t6)> CONCAT(function, _hook);        \
  typedef ret(WINAPI *CONCAT(function, _hooktype))(t1, t2, t3, t4, t5, t6);             \
  static ret WINAPI CONCAT(function, _hooked)(t1 p1, t2 p2, t3 p3, t4 p4, t5 p5, t6 p6) \
  {                                                                                     \
    SCOPED_LOCK(glLock);                                                                \
    if(!glhooks.m_HaveContextCreation)                                                  \
      return GL.function(p1, p2, p3, p4, p5, p6);                                       \
    gl_CurChunk = GLChunk::function;                                                    \
    return glhooks.GetDriver()->function(p1, p2, p3, p4, p5, p6);                       \
  }

#define HookWrapper7(ret, function, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6, p6, t7, p7)    \
  HookedFunction<ret(WINAPI *)(t1, t2, t3, t4, t5, t6, t7)> CONCAT(function, _hook);           \
  typedef ret(WINAPI *CONCAT(function, _hooktype))(t1, t2, t3, t4, t5, t6, t7);                \
  static ret WINAPI CONCAT(function, _hooked)(t1 p1, t2 p2, t3 p3, t4 p4, t5 p5, t6 p6, t7 p7) \
  {                                                                                            \
    SCOPED_LOCK(glLock);                                                                       \
    if(!glhooks.m_HaveContextCreation)                                                         \
      return GL.function(p1, p2, p3, p4, p5, p6, p7);                                          \
    gl_CurChunk = GLChunk::function;                                                           \
    return glhooks.GetDriver()->function(p1, p2, p3, p4, p5, p6, p7);                          \
  }

#define HookWrapper8(ret, function, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6, p6, t7, p7, t8, p8)   \
  HookedFunction<ret(WINAPI *)(t1, t2, t3, t4, t5, t6, t7, t8)> CONCAT(function, _hook);              \
  typedef ret(WINAPI *CONCAT(function, _hooktype))(t1, t2, t3, t4, t5, t6, t7, t8);                   \
  static ret WINAPI CONCAT(function, _hooked)(t1 p1, t2 p2, t3 p3, t4 p4, t5 p5, t6 p6, t7 p7, t8 p8) \
  {                                                                                                   \
    SCOPED_LOCK(glLock);                                                                              \
    if(!glhooks.m_HaveContextCreation)                                                                \
      return GL.function(p1, p2, p3, p4, p5, p6, p7, p8);                                             \
    gl_CurChunk = GLChunk::function;                                                                  \
    return glhooks.GetDriver()->function(p1, p2, p3, p4, p5, p6, p7, p8);                             \
  }

#define HookWrapper9(ret, function, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6, p6, t7, p7, t8, \
                     p8, t9, p9)                                                                \
  HookedFunction<ret(WINAPI *)(t1, t2, t3, t4, t5, t6, t7, t8, t9)> CONCAT(function, _hook);    \
  typedef ret(WINAPI *CONCAT(function, _hooktype))(t1, t2, t3, t4, t5, t6, t7, t8, t9);         \
  static ret WINAPI CONCAT(function, _hooked)(t1 p1, t2 p2, t3 p3, t4 p4, t5 p5, t6 p6, t7 p7,  \
                                              t8 p8, t9 p9)                                     \
  {                                                                                             \
    SCOPED_LOCK(glLock);                                                                        \
    if(!glhooks.m_HaveContextCreation)                                                          \
      return GL.function(p1, p2, p3, p4, p5, p6, p7, p8, p9);                                   \
    gl_CurChunk = GLChunk::function;                                                            \
    return glhooks.GetDriver()->function(p1, p2, p3, p4, p5, p6, p7, p8, p9);                   \
  }

#define HookWrapper10(ret, function, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6, p6, t7, p7, t8,  \
                      p8, t9, p9, t10, p10)                                                       \
  HookedFunction<ret(WINAPI *)(t1, t2, t3, t4, t5, t6, t7, t8, t9, t10)> CONCAT(function, _hook); \
  typedef ret(WINAPI *CONCAT(function, _hooktype))(t1, t2, t3, t4, t5, t6, t7, t8, t9, t10);      \
  static ret WINAPI CONCAT(function, _hooked)(t1 p1, t2 p2, t3 p3, t4 p4, t5 p5, t6 p6, t7 p7,    \
                                              t8 p8, t9 p9, t10 p10)                              \
  {                                                                                               \
    SCOPED_LOCK(glLock);                                                                          \
    if(!glhooks.m_HaveContextCreation)                                                            \
      return GL.function(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10);                                \
    gl_CurChunk = GLChunk::function;                                                              \
    return glhooks.GetDriver()->function(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10);                \
  }

#define HookWrapper11(ret, function, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6, p6, t7, p7, t8,  \
                      p8, t9, p9, t10, p10, t11, p11)                                             \
  HookedFunction<ret(WINAPI *)(t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11)> CONCAT(function,    \
                                                                                     _hook);      \
  typedef ret(WINAPI *CONCAT(function, _hooktype))(t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11); \
  static ret WINAPI CONCAT(function, _hooked)(t1 p1, t2 p2, t3 p3, t4 p4, t5 p5, t6 p6, t7 p7,    \
                                              t8 p8, t9 p9, t10 p10, t11 p11)                     \
  {                                                                                               \
    SCOPED_LOCK(glLock);                                                                          \
    if(!glhooks.m_HaveContextCreation)                                                            \
      return GL.function(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11);                           \
    gl_CurChunk = GLChunk::function;                                                              \
    return glhooks.GetDriver()->function(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11);           \
  }

#define HookWrapper12(ret, function, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6, p6, t7, p7, t8, \
                      p8, t9, p9, t10, p10, t11, p11, t12, p12)                                  \
  HookedFunction<ret(WINAPI *)(t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11, t12)> CONCAT(       \
      function, _hook);                                                                          \
  typedef ret(WINAPI *CONCAT(function, _hooktype))(t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11, \
                                                   t12);                                         \
  static ret WINAPI CONCAT(function, _hooked)(t1 p1, t2 p2, t3 p3, t4 p4, t5 p5, t6 p6, t7 p7,   \
                                              t8 p8, t9 p9, t10 p10, t11 p11, t12 p12)           \
  {                                                                                              \
    SCOPED_LOCK(glLock);                                                                         \
    if(!glhooks.m_HaveContextCreation)                                                           \
      return GL.function(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12);                     \
    gl_CurChunk = GLChunk::function;                                                             \
    return glhooks.GetDriver()->function(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12);     \
  }

#define HookWrapper13(ret, function, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6, p6, t7, p7, t8,  \
                      p8, t9, p9, t10, p10, t11, p11, t12, p12, t13, p13)                         \
  HookedFunction<ret(WINAPI *)(t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11, t12, t13)> CONCAT(   \
      function, _hook);                                                                           \
  typedef ret(WINAPI *CONCAT(function, _hooktype))(t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11,  \
                                                   t12, t13);                                     \
  static ret WINAPI CONCAT(function, _hooked)(t1 p1, t2 p2, t3 p3, t4 p4, t5 p5, t6 p6, t7 p7,    \
                                              t8 p8, t9 p9, t10 p10, t11 p11, t12 p12, t13 p13)   \
  {                                                                                               \
    SCOPED_LOCK(glLock);                                                                          \
    if(!glhooks.m_HaveContextCreation)                                                            \
      return GL.function(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13);                 \
    gl_CurChunk = GLChunk::function;                                                              \
    return glhooks.GetDriver()->function(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13); \
  }

#define HookWrapper14(ret, function, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6, p6, t7, p7, t8,     \
                      p8, t9, p9, t10, p10, t11, p11, t12, p12, t13, p13, t14, p14)                  \
  HookedFunction<ret(WINAPI *)(t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11, t12, t13, t14)> CONCAT( \
      function, _hook);                                                                              \
  typedef ret(WINAPI *CONCAT(function, _hooktype))(t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11,     \
                                                   t12, t13, t14);                                   \
  static ret WINAPI CONCAT(function, _hooked)(t1 p1, t2 p2, t3 p3, t4 p4, t5 p5, t6 p6, t7 p7,       \
                                              t8 p8, t9 p9, t10 p10, t11 p11, t12 p12, t13 p13,      \
                                              t14 p14)                                               \
  {                                                                                                  \
    SCOPED_LOCK(glLock);                                                                             \
    if(!glhooks.m_HaveContextCreation)                                                               \
      return GL.function(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14);               \
    gl_CurChunk = GLChunk::function;                                                                 \
    return glhooks.GetDriver()->function(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13,     \
                                         p14);                                                       \
  }

#define HookWrapper15(ret, function, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6, p6, t7, p7, t8,          \
                      p8, t9, p9, t10, p10, t11, p11, t12, p12, t13, p13, t14, p14, t15, p15)             \
  HookedFunction<ret(WINAPI *)(t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11, t12, t13, t14, t15)> CONCAT( \
      function, _hook);                                                                                   \
  typedef ret(WINAPI *CONCAT(function, _hooktype))(t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11,          \
                                                   t12, t13, t14, t15);                                   \
  static ret WINAPI CONCAT(function, _hooked)(t1 p1, t2 p2, t3 p3, t4 p4, t5 p5, t6 p6, t7 p7,            \
                                              t8 p8, t9 p9, t10 p10, t11 p11, t12 p12, t13 p13,           \
                                              t14 p14, t15 p15)                                           \
  {                                                                                                       \
    SCOPED_LOCK(glLock);                                                                                  \
    if(!glhooks.m_HaveContextCreation)                                                                    \
      return GL.function(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15);               \
    gl_CurChunk = GLChunk::function;                                                                      \
    return glhooks.GetDriver()->function(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13,          \
                                         p14, p15);                                                       \
  }

#define HookWrapper16(ret, function, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6, p6, t7, p7, t8,       \
                      p8, t9, p9, t10, p10, t11, p11, t12, p12, t13, p13, t14, p14, t15, p15, t16,     \
                      p16)                                                                             \
  HookedFunction<ret(WINAPI *)(t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11, t12, t13, t14, t15, t16)> \
      CONCAT(function, _hook);                                                                         \
  typedef ret(WINAPI *CONCAT(function, _hooktype))(t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11,       \
                                                   t12, t13, t14, t15, t16);                           \
  static ret WINAPI CONCAT(function, _hooked)(t1 p1, t2 p2, t3 p3, t4 p4, t5 p5, t6 p6, t7 p7,         \
                                              t8 p8, t9 p9, t10 p10, t11 p11, t12 p12, t13 p13,        \
                                              t14 p14, t15 p15, t16 p16)                               \
  {                                                                                                    \
    SCOPED_LOCK(glLock);                                                                               \
    if(!glhooks.m_HaveContextCreation)                                                                 \
      return GL.function(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16);       \
    gl_CurChunk = GLChunk::function;                                                                   \
    return glhooks.GetDriver()->function(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13,       \
                                         p14, p15, p16);                                               \
  }

#define HookWrapper17(ret, function, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6, p6, t7, p7, t8,   \
                      p8, t9, p9, t10, p10, t11, p11, t12, p12, t13, p13, t14, p14, t15, p15, t16, \
                      p16, t17, p17)                                                               \
  HookedFunction<ret(WINAPI *)(t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11, t12, t13, t14, t15,   \
                               t16, t17)>                                                          \
      CONCAT(function, _hook);                                                                     \
  typedef ret(WINAPI *CONCAT(function, _hooktype))(t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11,   \
                                                   t12, t13, t14, t15, t16, t17);                  \
  static ret WINAPI CONCAT(function, _hooked)(t1 p1, t2 p2, t3 p3, t4 p4, t5 p5, t6 p6, t7 p7,     \
                                              t8 p8, t9 p9, t10 p10, t11 p11, t12 p12, t13 p13,    \
                                              t14 p14, t15 p15, t16 p16, t17 p17)                  \
  {                                                                                                \
    SCOPED_LOCK(glLock);                                                                           \
    if(!glhooks.m_HaveContextCreation)                                                             \
      return GL.function(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16,    \
                         p17);                                                                     \
    gl_CurChunk = GLChunk::function;                                                               \
    return glhooks.GetDriver()->function(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13,   \
                                         p14, p15, p16, p17);                                      \
  }

#define HookAliasWrapper0(ret, function, realfunc)         \
  HookedFunction<ret(WINAPI *)()> CONCAT(function, _hook); \
  typedef ret(WINAPI *CONCAT(function, _hooktype))();      \
  static ret WINAPI CONCAT(function, _hooked)()            \
  {                                                        \
    SCOPED_LOCK(glLock);                                   \
    if(!glhooks.m_HaveContextCreation)                     \
      return GL.realfunc();                                \
    gl_CurChunk = GLChunk::function;                       \
    return glhooks.GetDriver()->realfunc();                \
  }

#define HookAliasWrapper1(ret, function, realfunc, t1, p1)   \
  HookedFunction<ret(WINAPI *)(t1)> CONCAT(function, _hook); \
  typedef ret(WINAPI *CONCAT(function, _hooktype))(t1);      \
  static ret WINAPI CONCAT(function, _hooked)(t1 p1)         \
  {                                                          \
    SCOPED_LOCK(glLock);                                     \
    if(!glhooks.m_HaveContextCreation)                       \
      return GL.realfunc(p1);                                \
    gl_CurChunk = GLChunk::function;                         \
    return glhooks.GetDriver()->realfunc(p1);                \
  }

#define HookAliasWrapper2(ret, function, realfunc, t1, p1, t2, p2) \
  HookedFunction<ret(WINAPI *)(t1, t2)> CONCAT(function, _hook);   \
  typedef ret(WINAPI *CONCAT(function, _hooktype))(t1, t2);        \
  static ret WINAPI CONCAT(function, _hooked)(t1 p1, t2 p2)        \
  {                                                                \
    SCOPED_LOCK(glLock);                                           \
    if(!glhooks.m_HaveContextCreation)                             \
      return GL.realfunc(p1, p2);                                  \
    gl_CurChunk = GLChunk::function;                               \
    return glhooks.GetDriver()->realfunc(p1, p2);                  \
  }

#define HookAliasWrapper3(ret, function, realfunc, t1, p1, t2, p2, t3, p3) \
  HookedFunction<ret(WINAPI *)(t1, t2, t3)> CONCAT(function, _hook);       \
  typedef ret(WINAPI *CONCAT(function, _hooktype))(t1, t2, t3);            \
  static ret WINAPI CONCAT(function, _hooked)(t1 p1, t2 p2, t3 p3)         \
  {                                                                        \
    SCOPED_LOCK(glLock);                                                   \
    if(!glhooks.m_HaveContextCreation)                                     \
      return GL.realfunc(p1, p2, p3);                                      \
    gl_CurChunk = GLChunk::function;                                       \
    return glhooks.GetDriver()->realfunc(p1, p2, p3);                      \
  }

#define HookAliasWrapper4(ret, function, realfunc, t1, p1, t2, p2, t3, p3, t4, p4) \
  HookedFunction<ret(WINAPI *)(t1, t2, t3, t4)> CONCAT(function, _hook);           \
  typedef ret(WINAPI *CONCAT(function, _hooktype))(t1, t2, t3, t4);                \
  static ret WINAPI CONCAT(function, _hooked)(t1 p1, t2 p2, t3 p3, t4 p4)          \
  {                                                                                \
    SCOPED_LOCK(glLock);                                                           \
    if(!glhooks.m_HaveContextCreation)                                             \
      return GL.realfunc(p1, p2, p3, p4);                                          \
    gl_CurChunk = GLChunk::function;                                               \
    return glhooks.GetDriver()->realfunc(p1, p2, p3, p4);                          \
  }

#define HookAliasWrapper5(ret, function, realfunc, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5) \
  HookedFunction<ret(WINAPI *)(t1, t2, t3, t4, t5)> CONCAT(function, _hook);               \
  typedef ret(WINAPI *CONCAT(function, _hooktype))(t1, t2, t3, t4, t5);                    \
  static ret WINAPI CONCAT(function, _hooked)(t1 p1, t2 p2, t3 p3, t4 p4, t5 p5)           \
  {                                                                                        \
    SCOPED_LOCK(glLock);                                                                   \
    if(!glhooks.m_HaveContextCreation)                                                     \
      return GL.realfunc(p1, p2, p3, p4, p5);                                              \
    gl_CurChunk = GLChunk::function;                                                       \
    return glhooks.GetDriver()->realfunc(p1, p2, p3, p4, p5);                              \
  }

#define HookAliasWrapper6(ret, function, realfunc, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6, p6) \
  HookedFunction<ret(WINAPI *)(t1, t2, t3, t4, t5, t6)> CONCAT(function, _hook);                   \
  typedef ret(WINAPI *CONCAT(function, _hooktype))(t1, t2, t3, t4, t5, t6);                        \
  static ret WINAPI CONCAT(function, _hooked)(t1 p1, t2 p2, t3 p3, t4 p4, t5 p5, t6 p6)            \
  {                                                                                                \
    SCOPED_LOCK(glLock);                                                                           \
    if(!glhooks.m_HaveContextCreation)                                                             \
      return GL.realfunc(p1, p2, p3, p4, p5, p6);                                                  \
    gl_CurChunk = GLChunk::function;                                                               \
    return glhooks.GetDriver()->realfunc(p1, p2, p3, p4, p5, p6);                                  \
  }

#define HookAliasWrapper7(ret, function, realfunc, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6, p6, \
                          t7, p7)                                                                  \
  HookedFunction<ret(WINAPI *)(t1, t2, t3, t4, t5, t6, t7)> CONCAT(function, _hook);               \
  typedef ret(WINAPI *CONCAT(function, _hooktype))(t1, t2, t3, t4, t5, t6, t7);                    \
  static ret WINAPI CONCAT(function, _hooked)(t1 p1, t2 p2, t3 p3, t4 p4, t5 p5, t6 p6, t7 p7)     \
  {                                                                                                \
    SCOPED_LOCK(glLock);                                                                           \
    if(!glhooks.m_HaveContextCreation)                                                             \
      return GL.realfunc(p1, p2, p3, p4, p5, p6, p7);                                              \
    gl_CurChunk = GLChunk::function;                                                               \
    return glhooks.GetDriver()->realfunc(p1, p2, p3, p4, p5, p6, p7);                              \
  }

#define HookAliasWrapper8(ret, function, realfunc, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6, p6,    \
                          t7, p7, t8, p8)                                                             \
  HookedFunction<ret(WINAPI *)(t1, t2, t3, t4, t5, t6, t7, t8)> CONCAT(function, _hook);              \
  typedef ret(WINAPI *CONCAT(function, _hooktype))(t1, t2, t3, t4, t5, t6, t7, t8);                   \
  static ret WINAPI CONCAT(function, _hooked)(t1 p1, t2 p2, t3 p3, t4 p4, t5 p5, t6 p6, t7 p7, t8 p8) \
  {                                                                                                   \
    SCOPED_LOCK(glLock);                                                                              \
    if(!glhooks.m_HaveContextCreation)                                                                \
      return GL.realfunc(p1, p2, p3, p4, p5, p6, p7, p8);                                             \
    gl_CurChunk = GLChunk::function;                                                                  \
    return glhooks.GetDriver()->realfunc(p1, p2, p3, p4, p5, p6, p7, p8);                             \
  }

#define HookAliasWrapper9(ret, function, realfunc, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6, p6, \
                          t7, p7, t8, p8, t9, p9)                                                  \
  HookedFunction<ret(WINAPI *)(t1, t2, t3, t4, t5, t6, t7, t8, t9)> CONCAT(function, _hook);       \
  typedef ret(WINAPI *CONCAT(function, _hooktype))(t1, t2, t3, t4, t5, t6, t7, t8, t9);            \
  static ret WINAPI CONCAT(function, _hooked)(t1 p1, t2 p2, t3 p3, t4 p4, t5 p5, t6 p6, t7 p7,     \
                                              t8 p8, t9 p9)                                        \
  {                                                                                                \
    SCOPED_LOCK(glLock);                                                                           \
    if(!glhooks.m_HaveContextCreation)                                                             \
      return GL.realfunc(p1, p2, p3, p4, p5, p6, p7, p8, p9);                                      \
    gl_CurChunk = GLChunk::function;                                                               \
    return glhooks.GetDriver()->realfunc(p1, p2, p3, p4, p5, p6, p7, p8, p9);                      \
  }

#define HookAliasWrapper10(ret, function, realfunc, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6,   \
                           p6, t7, p7, t8, p8, t9, p9, t10, p10)                                  \
  HookedFunction<ret(WINAPI *)(t1, t2, t3, t4, t5, t6, t7, t8, t9, t10)> CONCAT(function, _hook); \
  typedef ret(WINAPI *CONCAT(function, _hooktype))(t1, t2, t3, t4, t5, t6, t7, t8, t9, t10);      \
  static ret WINAPI CONCAT(function, _hooked)(t1 p1, t2 p2, t3 p3, t4 p4, t5 p5, t6 p6, t7 p7,    \
                                              t8 p8, t9 p9, t10 p10)                              \
  {                                                                                               \
    SCOPED_LOCK(glLock);                                                                          \
    if(!glhooks.m_HaveContextCreation)                                                            \
      return GL.realfunc(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10);                                \
    gl_CurChunk = GLChunk::function;                                                              \
    return glhooks.GetDriver()->realfunc(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10);                \
  }

#define HookAliasWrapper11(ret, function, realfunc, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6,   \
                           p6, t7, p7, t8, p8, t9, p9, t10, p10, t11, p11)                        \
  HookedFunction<ret(WINAPI *)(t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11)> CONCAT(function,    \
                                                                                     _hook);      \
  typedef ret(WINAPI *CONCAT(function, _hooktype))(t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11); \
  static ret WINAPI CONCAT(function, _hooked)(t1 p1, t2 p2, t3 p3, t4 p4, t5 p5, t6 p6, t7 p7,    \
                                              t8 p8, t9 p9, t10 p10, t11 p11)                     \
  {                                                                                               \
    SCOPED_LOCK(glLock);                                                                          \
    if(!glhooks.m_HaveContextCreation)                                                            \
      return GL.realfunc(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11);                           \
    gl_CurChunk = GLChunk::function;                                                              \
    return glhooks.GetDriver()->realfunc(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11);           \
  }

#define HookAliasWrapper12(ret, function, realfunc, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6,  \
                           p6, t7, p7, t8, p8, t9, p9, t10, p10, t11, p11, t12, p12)             \
  HookedFunction<ret(WINAPI *)(t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11, t12)> CONCAT(       \
      function, _hook);                                                                          \
  typedef ret(WINAPI *CONCAT(function, _hooktype))(t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11, \
                                                   t12);                                         \
  static ret WINAPI CONCAT(function, _hooked)(t1 p1, t2 p2, t3 p3, t4 p4, t5 p5, t6 p6, t7 p7,   \
                                              t8 p8, t9 p9, t10 p10, t11 p11, t12 p12)           \
  {                                                                                              \
    SCOPED_LOCK(glLock);                                                                         \
    if(!glhooks.m_HaveContextCreation)                                                           \
      return GL.realfunc(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12);                     \
    gl_CurChunk = GLChunk::function;                                                             \
    return glhooks.GetDriver()->realfunc(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12);     \
  }

#define HookAliasWrapper13(ret, function, realfunc, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6,   \
                           p6, t7, p7, t8, p8, t9, p9, t10, p10, t11, p11, t12, p12, t13, p13)    \
  HookedFunction<ret(WINAPI *)(t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11, t12, t13)> CONCAT(   \
      function, _hook);                                                                           \
  typedef ret(WINAPI *CONCAT(function, _hooktype))(t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11,  \
                                                   t12, t13);                                     \
  static ret WINAPI CONCAT(function, _hooked)(t1 p1, t2 p2, t3 p3, t4 p4, t5 p5, t6 p6, t7 p7,    \
                                              t8 p8, t9 p9, t10 p10, t11 p11, t12 p12, t13 p13)   \
  {                                                                                               \
    SCOPED_LOCK(glLock);                                                                          \
    if(!glhooks.m_HaveContextCreation)                                                            \
      return GL.realfunc(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13);                 \
    gl_CurChunk = GLChunk::function;                                                              \
    return glhooks.GetDriver()->realfunc(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13); \
  }

#define HookAliasWrapper14(ret, function, realfunc, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6,      \
                           p6, t7, p7, t8, p8, t9, p9, t10, p10, t11, p11, t12, p12, t13, p13,       \
                           t14, p14)                                                                 \
  HookedFunction<ret(WINAPI *)(t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11, t12, t13, t14)> CONCAT( \
      function, _hook);                                                                              \
  typedef ret(WINAPI *CONCAT(function, _hooktype))(t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11,     \
                                                   t12, t13, t14);                                   \
  static ret WINAPI CONCAT(function, _hooked)(t1 p1, t2 p2, t3 p3, t4 p4, t5 p5, t6 p6, t7 p7,       \
                                              t8 p8, t9 p9, t10 p10, t11 p11, t12 p12, t13 p13,      \
                                              t14 p14)                                               \
  {                                                                                                  \
    SCOPED_LOCK(glLock);                                                                             \
    if(!glhooks.m_HaveContextCreation)                                                               \
      return GL.realfunc(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14);               \
    gl_CurChunk = GLChunk::function;                                                                 \
    return glhooks.GetDriver()->realfunc(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13,     \
                                         p14);                                                       \
  }

#define HookAliasWrapper15(ret, function, realfunc, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6,           \
                           p6, t7, p7, t8, p8, t9, p9, t10, p10, t11, p11, t12, p12, t13, p13,            \
                           t14, p14, t15, p15)                                                            \
  HookedFunction<ret(WINAPI *)(t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11, t12, t13, t14, t15)> CONCAT( \
      function, _hook);                                                                                   \
  typedef ret(WINAPI *CONCAT(function, _hooktype))(t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11,          \
                                                   t12, t13, t14, t15);                                   \
  static ret WINAPI CONCAT(function, _hooked)(t1 p1, t2 p2, t3 p3, t4 p4, t5 p5, t6 p6, t7 p7,            \
                                              t8 p8, t9 p9, t10 p10, t11 p11, t12 p12, t13 p13,           \
                                              t14 p14, t15 p15)                                           \
  {                                                                                                       \
    SCOPED_LOCK(glLock);                                                                                  \
    if(!glhooks.m_HaveContextCreation)                                                                    \
      return GL.realfunc(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15);               \
    gl_CurChunk = GLChunk::function;                                                                      \
    return glhooks.GetDriver()->realfunc(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13,          \
                                         p14, p15);                                                       \
  }

#define HookAliasWrapper16(ret, function, realfunc, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6,        \
                           p6, t7, p7, t8, p8, t9, p9, t10, p10, t11, p11, t12, p12, t13, p13,         \
                           t14, p14, t15, p15, t16, p16)                                               \
  HookedFunction<ret(WINAPI *)(t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11, t12, t13, t14, t15, t16)> \
      CONCAT(function, _hook);                                                                         \
  typedef ret(WINAPI *CONCAT(function, _hooktype))(t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11,       \
                                                   t12, t13, t14, t15, t16);                           \
  static ret WINAPI CONCAT(function, _hooked)(t1 p1, t2 p2, t3 p3, t4 p4, t5 p5, t6 p6, t7 p7,         \
                                              t8 p8, t9 p9, t10 p10, t11 p11, t12 p12, t13 p13,        \
                                              t14 p14, t15 p15, t16 p16)                               \
  {                                                                                                    \
    SCOPED_LOCK(glLock);                                                                               \
    if(!glhooks.m_HaveContextCreation)                                                                 \
      return GL.realfunc(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16);       \
    gl_CurChunk = GLChunk::function;                                                                   \
    return glhooks.GetDriver()->realfunc(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13,       \
                                         p14, p15, p16);                                               \
  }

#define HookAliasWrapper17(ret, function, realfunc, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6,  \
                           p6, t7, p7, t8, p8, t9, p9, t10, p10, t11, p11, t12, p12, t13, p13,   \
                           t14, p14, t15, p15, t16, p16, t17, p17)                               \
  HookedFunction<ret(WINAPI *)(t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11, t12, t13, t14, t15, \
                               t16, t17)>                                                        \
      CONCAT(function, _hook);                                                                   \
  typedef ret(WINAPI *CONCAT(function, _hooktype))(t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11, \
                                                   t12, t13, t14, t15, t16, t17);                \
  static ret WINAPI CONCAT(function, _hooked)(t1 p1, t2 p2, t3 p3, t4 p4, t5 p5, t6 p6, t7 p7,   \
                                              t8 p8, t9 p9, t10 p10, t11 p11, t12 p12, t13 p13,  \
                                              t14 p14, t15 p15, t16 p16, t17 p17)                \
  {                                                                                              \
    SCOPED_LOCK(glLock);                                                                         \
    if(!glhooks.m_HaveContextCreation)                                                           \
      return GL.realfunc(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16,  \
                         p17);                                                                   \
    gl_CurChunk = GLChunk::function;                                                             \
    return glhooks.GetDriver()->realfunc(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, \
                                         p14, p15, p16, p17);                                    \
  }

typedef BOOL(WINAPI *WGLMAKECURRENTPROC)(HDC, HGLRC);
typedef BOOL(WINAPI *WGLDELETECONTEXTPROC)(HGLRC);

extern PFNWGLCREATECONTEXTATTRIBSARBPROC createContextAttribs;
extern PFNWGLGETPIXELFORMATATTRIBIVARBPROC getPixelFormatAttrib;
extern WGLMAKECURRENTPROC wglMakeCurrentProc;
extern WGLDELETECONTEXTPROC wglDeleteRC;

class OpenGLHook : LibraryHook, public GLPlatform
{
public:
  OpenGLHook()
  {
    m_GLDriver = NULL;

    m_HaveContextCreation = false;

    m_PopulatedHooks = false;

    m_CreatingContext = false;

    SetUnsupportedFunctionPointersToNULL();
  }
  ~OpenGLHook() { delete m_GLDriver; }
  void RegisterHooks()
  {
    LibraryHooks::RegisterLibraryHook("opengl32.dll", NULL);

    SetupHooks();
  }

  static OpenGLHook glhooks;

  void PopulateGLFunctions()
  {
    if(!m_PopulatedHooks)
      m_PopulatedHooks = PopulateHooks();
  }

  void MakeContextCurrent(GLWindowingData data)
  {
    if(wglMakeCurrent_hook())
      wglMakeCurrent_hook()(data.DC, data.ctx);
    else if(wglMakeCurrentProc)
      wglMakeCurrentProc(data.DC, data.ctx);
  }

  GLWindowingData MakeContext(GLWindowingData share)
  {
    GLWindowingData ret;
    if(wglCreateContextAttribsARB_realfunc)
    {
      const int attribs[] = {
          WGL_CONTEXT_MAJOR_VERSION_ARB,
          3,
          WGL_CONTEXT_MINOR_VERSION_ARB,
          2,
          WGL_CONTEXT_FLAGS_ARB,
          0,
          WGL_CONTEXT_PROFILE_MASK_ARB,
          WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
          0,
          0,
      };
      ret.DC = share.DC;
      m_CreatingContext = true;
      ret.ctx = wglCreateContextAttribsARB_realfunc(share.DC, share.ctx, attribs);
      m_CreatingContext = false;
    }
    return ret;
  }

  void DeleteContext(GLWindowingData context)
  {
    if(context.ctx && wglDeleteContext_hook())
      wglDeleteContext_hook()(context.ctx);
  }

  void DeleteReplayContext(GLWindowingData context)
  {
    if(wglDeleteRC)
    {
      wglMakeCurrentProc(NULL, NULL);
      wglDeleteRC(context.ctx);
      ReleaseDC(context.wnd, context.DC);
      ::DestroyWindow(context.wnd);
    }
  }

  void SwapBuffers(GLWindowingData context) { ::SwapBuffers(context.DC); }
  void GetOutputWindowDimensions(GLWindowingData context, int32_t &w, int32_t &h)
  {
    RECT rect = {0};
    GetClientRect(context.wnd, &rect);
    w = rect.right - rect.left;
    h = rect.bottom - rect.top;
  }

  bool IsOutputWindowVisible(GLWindowingData context)
  {
    return (IsWindowVisible(context.wnd) == TRUE);
  }

  GLWindowingData GLPlatform::MakeOutputWindow(WindowingData window, bool depth,
                                               GLWindowingData share_context)
  {
    GLWindowingData ret;

    RDCASSERT(window.system == WindowingSystem::Win32 || window.system == WindowingSystem::Unknown,
              window.system);

    HWND w = window.win32.window;

    if(w == NULL)
      w = CreateWindowEx(WS_EX_CLIENTEDGE, L"renderdocGLclass", L"", WS_OVERLAPPEDWINDOW,
                         CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL,
                         GetModuleHandle(NULL), NULL);

    HDC DC = GetDC(w);

    PIXELFORMATDESCRIPTOR pfd = {0};

    int attrib = eWGL_NUMBER_PIXEL_FORMATS_ARB;
    int value = 1;

    getPixelFormatAttrib(DC, 1, 0, 1, &attrib, &value);

    int pf = 0;

    int numpfs = value;
    for(int i = 1; i <= numpfs; i++)
    {
      // verify that we have the properties we want
      attrib = eWGL_DRAW_TO_WINDOW_ARB;
      getPixelFormatAttrib(DC, i, 0, 1, &attrib, &value);
      if(value == 0)
        continue;

      attrib = eWGL_ACCELERATION_ARB;
      getPixelFormatAttrib(DC, i, 0, 1, &attrib, &value);
      if(value == eWGL_NO_ACCELERATION_ARB)
        continue;

      attrib = eWGL_SUPPORT_OPENGL_ARB;
      getPixelFormatAttrib(DC, i, 0, 1, &attrib, &value);
      if(value == 0)
        continue;

      attrib = eWGL_DOUBLE_BUFFER_ARB;
      getPixelFormatAttrib(DC, i, 0, 1, &attrib, &value);
      if(value == 0)
        continue;

      attrib = eWGL_PIXEL_TYPE_ARB;
      getPixelFormatAttrib(DC, i, 0, 1, &attrib, &value);
      if(value != eWGL_TYPE_RGBA_ARB)
        continue;

      // we have an opengl-capable accelerated RGBA context.
      // we use internal framebuffers to do almost all rendering, so we just need
      // RGB (color bits > 24) and SRGB buffer.

      attrib = eWGL_COLOR_BITS_ARB;
      getPixelFormatAttrib(DC, i, 0, 1, &attrib, &value);
      if(value < 24)
        continue;

      attrib = WGL_FRAMEBUFFER_SRGB_CAPABLE_ARB;
      getPixelFormatAttrib(DC, i, 0, 1, &attrib, &value);
      if(value == 0)
        continue;

      // this one suits our needs, choose it
      pf = i;
      break;
    }

    if(pf == 0)
    {
      ReleaseDC(w, DC);
      RDCERR("Couldn't choose pixel format");
      return ret;
    }

    BOOL res = DescribePixelFormat(DC, pf, sizeof(pfd), &pfd);
    if(res == FALSE)
    {
      ReleaseDC(w, DC);
      RDCERR("Couldn't describe pixel format");
      return ret;
    }

    res = SetPixelFormat(DC, pf, &pfd);
    if(res == FALSE)
    {
      ReleaseDC(w, DC);
      RDCERR("Couldn't set pixel format");
      return ret;
    }

    int attribs[64] = {0};
    int i = 0;

    attribs[i++] = WGL_CONTEXT_MAJOR_VERSION_ARB;
    attribs[i++] = GLCoreVersion / 10;
    attribs[i++] = WGL_CONTEXT_MINOR_VERSION_ARB;
    attribs[i++] = GLCoreVersion % 10;
    attribs[i++] = WGL_CONTEXT_FLAGS_ARB;
#if ENABLED(RDOC_DEVEL)
    attribs[i++] = WGL_CONTEXT_DEBUG_BIT_ARB;
#else
    attribs[i++] = 0;
#endif
    attribs[i++] = WGL_CONTEXT_PROFILE_MASK_ARB;
    attribs[i++] = WGL_CONTEXT_CORE_PROFILE_BIT_ARB;

    HGLRC rc = createContextAttribs(DC, share_context.ctx, attribs);
    if(rc == NULL)
    {
      ReleaseDC(w, DC);
      RDCERR("Couldn't create %d.%d context - something changed since creation", GLCoreVersion / 10,
             GLCoreVersion % 10);
      return ret;
    }

    ret.DC = DC;
    ret.ctx = rc;
    ret.wnd = w;

    return ret;
  }

  bool DrawQuads(float width, float height, const std::vector<Vec4f> &vertices);

private:
  WrappedOpenGL *GetDriver()
  {
    if(m_GLDriver == NULL)
      m_GLDriver = new WrappedOpenGL(*this);

    return m_GLDriver;
  }

  // we use this to check if we've seen a context be created.
  // If we HAVEN'T then RenderDoc was probably injected after
  // the start of the application so we should not call our
  // hooked functions - things will go wrong like missing
  // context data, references to resources we don't know about
  // and hooked functions via wglGetProcAddress being NULL
  // and never being called by the app.
  bool m_HaveContextCreation;

  HookedFunction<HGLRC(WINAPI *)(HDC)> wglCreateContext_hook;
  HookedFunction<BOOL(WINAPI *)(HGLRC)> wglDeleteContext_hook;
  HookedFunction<HGLRC(WINAPI *)(HDC, int)> wglCreateLayerContext_hook;
  HookedFunction<BOOL(WINAPI *)(HDC, HGLRC)> wglMakeCurrent_hook;
  HookedFunction<PROC(WINAPI *)(const char *)> wglGetProcAddress_hook;
  HookedFunction<BOOL(WINAPI *)(HDC)> SwapBuffers_hook;
  HookedFunction<BOOL(WINAPI *)(HDC)> wglSwapBuffers_hook;
  HookedFunction<BOOL(WINAPI *)(HDC, UINT)> wglSwapLayerBuffers_hook;
  HookedFunction<BOOL(WINAPI *)(UINT, CONST WGLSWAP *)> wglSwapMultipleBuffers_hook;
  HookedFunction<LONG(WINAPI *)(DEVMODEA *, DWORD)> ChangeDisplaySettingsA_hook;
  HookedFunction<LONG(WINAPI *)(DEVMODEW *, DWORD)> ChangeDisplaySettingsW_hook;
  HookedFunction<LONG(WINAPI *)(LPCSTR, DEVMODEA *, HWND, DWORD, LPVOID)> ChangeDisplaySettingsExA_hook;
  HookedFunction<LONG(WINAPI *)(LPCWSTR, DEVMODEW *, HWND, DWORD, LPVOID)> ChangeDisplaySettingsExW_hook;

  PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB_realfunc;
  PFNWGLCHOOSEPIXELFORMATARBPROC wglChoosePixelFormatARB_realfunc;
  PFNWGLGETPIXELFORMATATTRIBFVARBPROC wglGetPixelFormatAttribfvARB_realfunc;
  PFNWGLGETPIXELFORMATATTRIBIVARBPROC wglGetPixelFormatAttribivARB_realfunc;

  static GLInitParams GetInitParamsForDC(HDC dc)
  {
    GLInitParams ret;

    int pf = GetPixelFormat(dc);

    PIXELFORMATDESCRIPTOR pfd;
    DescribePixelFormat(dc, pf, sizeof(PIXELFORMATDESCRIPTOR), &pfd);

    HWND w = WindowFromDC(dc);

    RECT r;
    GetClientRect(w, &r);

    RDCLOG("dc %p. PFD: type %d, %d color bits, %d depth bits, %d stencil bits. Win: %dx%d", dc,
           pfd.iPixelType, pfd.cColorBits, pfd.cDepthBits, pfd.cStencilBits, r.right - r.left,
           r.bottom - r.top);

    ret.colorBits = pfd.cColorBits;
    ret.depthBits = pfd.cDepthBits;
    ret.stencilBits = pfd.cStencilBits;
    ret.width = (r.right - r.left);
    ret.height = (r.bottom - r.top);

    ret.isSRGB = true;

    if(glhooks.wglGetProcAddress_hook() == NULL)
      glhooks.wglGetProcAddress_hook.SetFuncPtr(
          Process::GetFunctionAddress(Process::LoadModule("opengl32.dll"), "wglGetProcAddress"));

    if(glhooks.wglGetPixelFormatAttribivARB_realfunc == NULL)
      glhooks.wglGetProcAddress_hook()("wglGetPixelFormatAttribivARB");

    if(glhooks.wglGetPixelFormatAttribivARB_realfunc)
    {
      int attrname = eWGL_FRAMEBUFFER_SRGB_CAPABLE_ARB;
      int srgb = 1;
      glhooks.wglGetPixelFormatAttribivARB_realfunc(dc, pf, 0, 1, &attrname, &srgb);
      ret.isSRGB = srgb;

      attrname = eWGL_SAMPLES_ARB;
      int ms = 1;
      glhooks.wglGetPixelFormatAttribivARB_realfunc(dc, pf, 0, 1, &attrname, &ms);
      ret.multiSamples = RDCMAX(1, ms);
    }

    if(pfd.iPixelType != PFD_TYPE_RGBA)
    {
      RDCERR("Unsupported OpenGL pixel type");
    }

    return ret;
  }

  bool m_CreatingContext;

  static HGLRC WINAPI wglCreateContext_hooked(HDC dc)
  {
    HGLRC ret = glhooks.wglCreateContext_hook()(dc);

    DWORD err = GetLastError();

    // don't recurse and don't continue if creation failed
    if(glhooks.m_CreatingContext || ret == NULL)
      return ret;

    glhooks.m_CreatingContext = true;

    GLWindowingData data;
    data.DC = dc;
    data.wnd = WindowFromDC(dc);
    data.ctx = ret;

    glhooks.GetDriver()->CreateContext(data, NULL, GetInitParamsForDC(dc), false, false);

    glhooks.m_HaveContextCreation = true;

    SetLastError(err);

    glhooks.m_CreatingContext = false;

    return ret;
  }

  static BOOL WINAPI wglDeleteContext_hooked(HGLRC rc)
  {
    if(glhooks.m_HaveContextCreation)
      glhooks.GetDriver()->DeleteContext(rc);

    SetLastError(0);

    return glhooks.wglDeleteContext_hook()(rc);
  }

  static HGLRC WINAPI wglCreateLayerContext_hooked(HDC dc, int iLayerPlane)
  {
    HGLRC ret = glhooks.wglCreateLayerContext_hook()(dc, iLayerPlane);

    DWORD err = GetLastError();

    // don't recurse and don't continue if creation failed
    if(glhooks.m_CreatingContext || ret == NULL)
      return ret;

    glhooks.m_CreatingContext = true;

    GLWindowingData data;
    data.DC = dc;
    data.wnd = WindowFromDC(dc);
    data.ctx = ret;

    glhooks.GetDriver()->CreateContext(data, NULL, GetInitParamsForDC(dc), false, false);

    glhooks.m_HaveContextCreation = true;

    SetLastError(err);

    glhooks.m_CreatingContext = false;

    return ret;
  }

  static HGLRC WINAPI wglCreateContextAttribsARB_hooked(HDC dc, HGLRC hShareContext,
                                                        const int *attribList)
  {
    // don't recurse
    if(glhooks.m_CreatingContext)
      return glhooks.wglCreateContextAttribsARB_realfunc(dc, hShareContext, attribList);

    glhooks.m_CreatingContext = true;

    int defaultAttribList[] = {0};

    const int *attribs = attribList ? attribList : defaultAttribList;
    vector<int> attribVec;

    // modify attribs to our liking
    {
      bool flagsFound = false;
      const int *a = attribs;
      while(*a)
      {
        int name = *a++;
        int val = *a++;

        if(name == WGL_CONTEXT_FLAGS_ARB)
        {
          if(RenderDoc::Inst().GetCaptureOptions().apiValidation)
            val |= WGL_CONTEXT_DEBUG_BIT_ARB;
          else
            val &= ~WGL_CONTEXT_DEBUG_BIT_ARB;

          // remove NO_ERROR bit
          val &= ~GL_CONTEXT_FLAG_NO_ERROR_BIT_KHR;

          flagsFound = true;
        }

        attribVec.push_back(name);
        attribVec.push_back(val);
      }

      if(!flagsFound && RenderDoc::Inst().GetCaptureOptions().apiValidation)
      {
        attribVec.push_back(WGL_CONTEXT_FLAGS_ARB);
        attribVec.push_back(WGL_CONTEXT_DEBUG_BIT_ARB);
      }

      attribVec.push_back(0);

      attribs = &attribVec[0];
    }

    RDCDEBUG("wglCreateContextAttribsARB:");

    bool core = false;

    int *a = (int *)attribs;
    while(*a)
    {
      RDCDEBUG("%x: %d", a[0], a[1]);

      if(a[0] == WGL_CONTEXT_PROFILE_MASK_ARB)
        core = (a[1] & WGL_CONTEXT_CORE_PROFILE_BIT_ARB);

      a += 2;
    }

    SetLastError(0);

    HGLRC ret = glhooks.wglCreateContextAttribsARB_realfunc(dc, hShareContext, attribs);

    DWORD err = GetLastError();

    // don't continue if creation failed
    if(ret == NULL)
    {
      glhooks.m_CreatingContext = false;
      return ret;
    }

    GLWindowingData data;
    data.DC = dc;
    data.wnd = WindowFromDC(dc);
    data.ctx = ret;

    glhooks.GetDriver()->CreateContext(data, hShareContext, GetInitParamsForDC(dc), core, true);

    glhooks.m_HaveContextCreation = true;

    SetLastError(err);

    glhooks.m_CreatingContext = false;

    return ret;
  }

  static BOOL WINAPI wglChoosePixelFormatARB_hooked(HDC hdc, const int *piAttribIList,
                                                    const FLOAT *pfAttribFList, UINT nMaxFormats,
                                                    int *piFormats, UINT *nNumFormats)
  {
    return glhooks.wglChoosePixelFormatARB_realfunc(hdc, piAttribIList, pfAttribFList, nMaxFormats,
                                                    piFormats, nNumFormats);
  }
  static BOOL WINAPI wglGetPixelFormatAttribfvARB_hooked(HDC hdc, int iPixelFormat, int iLayerPlane,
                                                         UINT nAttributes, const int *piAttributes,
                                                         FLOAT *pfValues)
  {
    return glhooks.wglGetPixelFormatAttribfvARB_realfunc(hdc, iPixelFormat, iLayerPlane,
                                                         nAttributes, piAttributes, pfValues);
  }
  static BOOL WINAPI wglGetPixelFormatAttribivARB_hooked(HDC hdc, int iPixelFormat, int iLayerPlane,
                                                         UINT nAttributes, const int *piAttributes,
                                                         int *piValues)
  {
    return glhooks.wglGetPixelFormatAttribivARB_realfunc(hdc, iPixelFormat, iLayerPlane,
                                                         nAttributes, piAttributes, piValues);
  }

  // wglShareLists_hooked ?

  static BOOL WINAPI wglMakeCurrent_hooked(HDC dc, HGLRC rc)
  {
    BOOL ret = glhooks.wglMakeCurrent_hook()(dc, rc);

    DWORD err = GetLastError();

    {
      SCOPED_LOCK(glLock);

      if(rc && glhooks.m_HaveContextCreation &&
         glhooks.m_Contexts.find(rc) == glhooks.m_Contexts.end())
      {
        glhooks.m_Contexts.insert(rc);

        glhooks.PopulateHooks();
      }

      GLWindowingData data;
      data.DC = dc;
      data.wnd = WindowFromDC(dc);
      data.ctx = rc;

      if(glhooks.m_HaveContextCreation)
        glhooks.GetDriver()->ActivateContext(data);
    }

    SetLastError(err);

    return ret;
  }

  // Make sure that even if internally SwapBuffers calls wglSwapBuffers we don't process both of
  // them separately
  bool m_InSwap = false;

  static void ProcessSwapBuffers(HDC dc)
  {
    HWND w = WindowFromDC(dc);

    if(w != NULL && glhooks.m_HaveContextCreation && !glhooks.m_InSwap)
    {
      SCOPED_LOCK(glLock);

      RECT r;
      GetClientRect(w, &r);

      glhooks.GetDriver()->WindowSize(w, r.right - r.left, r.bottom - r.top);

      glhooks.GetDriver()->SwapBuffers(w);

      SetLastError(0);
    }
  }

  static BOOL WINAPI SwapBuffers_hooked(HDC dc)
  {
    SCOPED_LOCK(glLock);

    ProcessSwapBuffers(dc);

    glhooks.m_InSwap = true;
    BOOL ret = glhooks.SwapBuffers_hook()(dc);
    glhooks.m_InSwap = false;

    return ret;
  }

  static BOOL WINAPI wglSwapBuffers_hooked(HDC dc)
  {
    SCOPED_LOCK(glLock);

    ProcessSwapBuffers(dc);

    glhooks.m_InSwap = true;
    BOOL ret = glhooks.wglSwapBuffers_hook()(dc);
    glhooks.m_InSwap = false;

    return ret;
  }

  static BOOL WINAPI wglSwapLayerBuffers_hooked(HDC dc, UINT planes)
  {
    SCOPED_LOCK(glLock);

    ProcessSwapBuffers(dc);

    glhooks.m_InSwap = true;
    BOOL ret = glhooks.wglSwapLayerBuffers_hook()(dc, planes);
    glhooks.m_InSwap = false;

    return ret;
  }

  static BOOL WINAPI wglSwapMultipleBuffers_hooked(UINT numSwaps, CONST WGLSWAP *pSwaps)
  {
    for(UINT i = 0; pSwaps && i < numSwaps; i++)
      ProcessSwapBuffers(pSwaps[i].hdc);

    glhooks.m_InSwap = true;
    BOOL ret = glhooks.wglSwapMultipleBuffers_hook()(numSwaps, pSwaps);
    glhooks.m_InSwap = false;

    return ret;
  }

  static LONG WINAPI ChangeDisplaySettingsA_hooked(DEVMODEA *mode, DWORD flags)
  {
    if((flags & CDS_FULLSCREEN) == 0 || RenderDoc::Inst().GetCaptureOptions().allowFullscreen)
      return glhooks.ChangeDisplaySettingsA_hook()(mode, flags);

    return DISP_CHANGE_SUCCESSFUL;
  }

  static LONG WINAPI ChangeDisplaySettingsW_hooked(DEVMODEW *mode, DWORD flags)
  {
    if((flags & CDS_FULLSCREEN) == 0 || RenderDoc::Inst().GetCaptureOptions().allowFullscreen)
      return glhooks.ChangeDisplaySettingsW_hook()(mode, flags);

    return DISP_CHANGE_SUCCESSFUL;
  }

  static LONG WINAPI ChangeDisplaySettingsExA_hooked(LPCSTR devname, DEVMODEA *mode, HWND wnd,
                                                     DWORD flags, LPVOID param)
  {
    if((flags & CDS_FULLSCREEN) == 0 || RenderDoc::Inst().GetCaptureOptions().allowFullscreen)
      return glhooks.ChangeDisplaySettingsExA_hook()(devname, mode, wnd, flags, param);

    return DISP_CHANGE_SUCCESSFUL;
  }

  static LONG WINAPI ChangeDisplaySettingsExW_hooked(LPCWSTR devname, DEVMODEW *mode, HWND wnd,
                                                     DWORD flags, LPVOID param)
  {
    if((flags & CDS_FULLSCREEN) == 0 || RenderDoc::Inst().GetCaptureOptions().allowFullscreen)
      return glhooks.ChangeDisplaySettingsExW_hook()(devname, mode, wnd, flags, param);

    return DISP_CHANGE_SUCCESSFUL;
  }

  static PROC WINAPI wglGetProcAddress_hooked(const char *func)
  {
    PROC realFunc = glhooks.wglGetProcAddress_hook()(func);

#if 0
			RDCDEBUG("Checking for extension - %s - real function is %p", func, realFunc);
#endif

    // if the real RC doesn't support this function, don't bother hooking
    if(realFunc == NULL)
      return realFunc;

    if(!strcmp(func, "wglCreateContextAttribsARB"))
    {
      glhooks.wglCreateContextAttribsARB_realfunc = (PFNWGLCREATECONTEXTATTRIBSARBPROC)realFunc;
      return (PROC)&wglCreateContextAttribsARB_hooked;
    }
    if(!strcmp(func, "wglChoosePixelFormatARB"))
    {
      glhooks.wglChoosePixelFormatARB_realfunc = (PFNWGLCHOOSEPIXELFORMATARBPROC)realFunc;
      return (PROC)&wglChoosePixelFormatARB_hooked;
    }
    if(!strcmp(func, "wglGetPixelFormatAttribfvARB"))
    {
      glhooks.wglGetPixelFormatAttribfvARB_realfunc = (PFNWGLGETPIXELFORMATATTRIBFVARBPROC)realFunc;
      return (PROC)&wglGetPixelFormatAttribfvARB_hooked;
    }
    if(!strcmp(func, "wglGetPixelFormatAttribivARB"))
    {
      glhooks.wglGetPixelFormatAttribivARB_realfunc = (PFNWGLGETPIXELFORMATATTRIBIVARBPROC)realFunc;
      return (PROC)&wglGetPixelFormatAttribivARB_hooked;
    }

    HookCheckGLExtensions();

    // assume wgl functions are safe to just pass straight through
    if(!strncmp(func, "wgl", 3))
      return realFunc;

    CheckUnsupported();

    // for any other function, if it's not a core or extension function we know about,
    // just return NULL
    return NULL;
  }

  WrappedOpenGL *m_GLDriver;

  bool m_PopulatedHooks;

  set<HGLRC> m_Contexts;

  void SetupHooks()
  {
    wglCreateContext_hook.Register("opengl32.dll", "wglCreateContext", wglCreateContext_hooked);
    wglDeleteContext_hook.Register("opengl32.dll", "wglDeleteContext", wglDeleteContext_hooked);
    wglCreateLayerContext_hook.Register("opengl32.dll", "wglCreateLayerContext",
                                        wglCreateLayerContext_hooked);
    wglMakeCurrent_hook.Register("opengl32.dll", "wglMakeCurrent", wglMakeCurrent_hooked);

    wglGetProcAddress_hook.Register("opengl32.dll", "wglGetProcAddress", wglGetProcAddress_hooked);
    wglSwapBuffers_hook.Register("opengl32.dll", "wglSwapBuffers", wglSwapBuffers_hooked);
    wglSwapLayerBuffers_hook.Register("opengl32.dll", "wglSwapLayerBuffers",
                                      wglSwapLayerBuffers_hooked);
    wglSwapMultipleBuffers_hook.Register("opengl32.dll", "wglSwapMultipleBuffers",
                                         wglSwapMultipleBuffers_hooked);
    SwapBuffers_hook.Register("gdi32.dll", "SwapBuffers", SwapBuffers_hooked);
    ChangeDisplaySettingsA_hook.Register("user32.dll", "ChangeDisplaySettingsA",
                                         ChangeDisplaySettingsA_hooked);
    ChangeDisplaySettingsW_hook.Register("user32.dll", "ChangeDisplaySettingsW",
                                         ChangeDisplaySettingsW_hooked);
    ChangeDisplaySettingsExA_hook.Register("user32.dll", "ChangeDisplaySettingsExA",
                                           ChangeDisplaySettingsExA_hooked);
    ChangeDisplaySettingsExW_hook.Register("user32.dll", "ChangeDisplaySettingsExW",
                                           ChangeDisplaySettingsExW_hooked);

    DLLExportHooks();
  }

  bool PopulateHooks()
  {
    void *moduleHandle = Process::LoadModule("opengl32.dll");

    if(wglGetProcAddress_hook() == NULL)
      wglGetProcAddress_hook.SetFuncPtr(
          Process::GetFunctionAddress(moduleHandle, "wglGetProcAddress"));

    wglGetProcAddress_hooked("wglCreateContextAttribsARB");

#undef HookInit
#define HookInit(function)                                                               \
  if(GL.function == NULL)                                                                \
    GL.function = (CONCAT(function, _hooktype))Process::GetFunctionAddress(moduleHandle, \
                                                                           STRINGIZE(function));

// cheeky
#undef HookExtension
#define HookExtension(funcPtrType, function) wglGetProcAddress_hooked(STRINGIZE(function))
#undef HookExtensionAlias
#define HookExtensionAlias(funcPtrType, function, alias)

    DLLExportHooks();
    HookCheckGLExtensions();

    CheckExtensions();

    // see gl_emulated.cpp
    GL.EmulateUnsupportedFunctions();
    GL.EmulateRequiredExtensions();

    return true;
  }

  DefineDLLExportHooks();
  DefineGLExtensionHooks();

/*
       in bash:

    function HookWrapper()
    {
        N=$1;
        echo "#undef HookWrapper$N";
        echo -n "#define HookWrapper$N(ret, function";
            for I in `seq 1 $N`; do echo -n ", t$I, p$I"; done;
        echo ") \\";

        echo -en "\ttypedef ret (WINAPI *CONCAT(function, _hooktype)) (";
            for I in `seq 1 $N`; do echo -n "t$I"; if [ $I -ne $N ]; then echo -n ", "; fi; done;
        echo "); \\";

        echo -en "\tCONCAT(function, _hooktype) CONCAT(unsupported_real_,function);";


        echo -en "\tstatic ret WINAPI CONCAT(function, _hooked)(";
            for I in `seq 1 $N`; do echo -n "t$I p$I"; if [ $I -ne $N ]; then echo -n ", "; fi;
  done;
        echo ") \\";

        echo -e "\t{ \\";
        echo -e "\tstatic bool hit = false; if(hit == false) { RDCERR(\"Function \" \\";
        echo -e "\t\tSTRINGIZE(function)\" not supported - capture may be broken\");hit = true;}\\";
        echo -en "\treturn glhooks.CONCAT(unsupported_real_,function)( ";
            for I in `seq 1 $N`; do echo -n "p$I"; if [ $I -ne $N ]; then echo -n ", "; fi; done;
        echo -e "); \\";
        echo -e "\t}";
    }

  for I in `seq 0 17`; do HookWrapper $I; echo; done

       */

#undef HookWrapper0
#define HookWrapper0(ret, function)                                                     \
  typedef ret(WINAPI *CONCAT(function, _hooktype))();                                   \
  CONCAT(function, _hooktype) CONCAT(unsupported_real_, function);                      \
  static ret WINAPI CONCAT(function, _hooked)()                                         \
  {                                                                                     \
    static bool hit = false;                                                            \
    if(hit == false)                                                                    \
    {                                                                                   \
      RDCERR("Function " STRINGIZE(function) " not supported - capture may be broken"); \
      hit = true;                                                                       \
    }                                                                                   \
    return glhooks.CONCAT(unsupported_real_, function)();                               \
  }

#undef HookWrapper1
#define HookWrapper1(ret, function, t1, p1)                                             \
  typedef ret(WINAPI *CONCAT(function, _hooktype))(t1);                                 \
  CONCAT(function, _hooktype) CONCAT(unsupported_real_, function);                      \
  static ret WINAPI CONCAT(function, _hooked)(t1 p1)                                    \
  {                                                                                     \
    static bool hit = false;                                                            \
    if(hit == false)                                                                    \
    {                                                                                   \
      RDCERR("Function " STRINGIZE(function) " not supported - capture may be broken"); \
      hit = true;                                                                       \
    }                                                                                   \
    return glhooks.CONCAT(unsupported_real_, function)(p1);                             \
  }

#undef HookWrapper2
#define HookWrapper2(ret, function, t1, p1, t2, p2)                                     \
  typedef ret(WINAPI *CONCAT(function, _hooktype))(t1, t2);                             \
  CONCAT(function, _hooktype) CONCAT(unsupported_real_, function);                      \
  static ret WINAPI CONCAT(function, _hooked)(t1 p1, t2 p2)                             \
  {                                                                                     \
    static bool hit = false;                                                            \
    if(hit == false)                                                                    \
    {                                                                                   \
      RDCERR("Function " STRINGIZE(function) " not supported - capture may be broken"); \
      hit = true;                                                                       \
    }                                                                                   \
    return glhooks.CONCAT(unsupported_real_, function)(p1, p2);                         \
  }

#undef HookWrapper3
#define HookWrapper3(ret, function, t1, p1, t2, p2, t3, p3)                             \
  typedef ret(WINAPI *CONCAT(function, _hooktype))(t1, t2, t3);                         \
  CONCAT(function, _hooktype) CONCAT(unsupported_real_, function);                      \
  static ret WINAPI CONCAT(function, _hooked)(t1 p1, t2 p2, t3 p3)                      \
  {                                                                                     \
    static bool hit = false;                                                            \
    if(hit == false)                                                                    \
    {                                                                                   \
      RDCERR("Function " STRINGIZE(function) " not supported - capture may be broken"); \
      hit = true;                                                                       \
    }                                                                                   \
    return glhooks.CONCAT(unsupported_real_, function)(p1, p2, p3);                     \
  }

#undef HookWrapper4
#define HookWrapper4(ret, function, t1, p1, t2, p2, t3, p3, t4, p4)                     \
  typedef ret(WINAPI *CONCAT(function, _hooktype))(t1, t2, t3, t4);                     \
  CONCAT(function, _hooktype) CONCAT(unsupported_real_, function);                      \
  static ret WINAPI CONCAT(function, _hooked)(t1 p1, t2 p2, t3 p3, t4 p4)               \
  {                                                                                     \
    static bool hit = false;                                                            \
    if(hit == false)                                                                    \
    {                                                                                   \
      RDCERR("Function " STRINGIZE(function) " not supported - capture may be broken"); \
      hit = true;                                                                       \
    }                                                                                   \
    return glhooks.CONCAT(unsupported_real_, function)(p1, p2, p3, p4);                 \
  }

#undef HookWrapper5
#define HookWrapper5(ret, function, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5)             \
  typedef ret(WINAPI *CONCAT(function, _hooktype))(t1, t2, t3, t4, t5);                 \
  CONCAT(function, _hooktype) CONCAT(unsupported_real_, function);                      \
  static ret WINAPI CONCAT(function, _hooked)(t1 p1, t2 p2, t3 p3, t4 p4, t5 p5)        \
  {                                                                                     \
    static bool hit = false;                                                            \
    if(hit == false)                                                                    \
    {                                                                                   \
      RDCERR("Function " STRINGIZE(function) " not supported - capture may be broken"); \
      hit = true;                                                                       \
    }                                                                                   \
    return glhooks.CONCAT(unsupported_real_, function)(p1, p2, p3, p4, p5);             \
  }

#undef HookWrapper6
#define HookWrapper6(ret, function, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6, p6)     \
  typedef ret(WINAPI *CONCAT(function, _hooktype))(t1, t2, t3, t4, t5, t6);             \
  CONCAT(function, _hooktype) CONCAT(unsupported_real_, function);                      \
  static ret WINAPI CONCAT(function, _hooked)(t1 p1, t2 p2, t3 p3, t4 p4, t5 p5, t6 p6) \
  {                                                                                     \
    static bool hit = false;                                                            \
    if(hit == false)                                                                    \
    {                                                                                   \
      RDCERR("Function " STRINGIZE(function) " not supported - capture may be broken"); \
      hit = true;                                                                       \
    }                                                                                   \
    return glhooks.CONCAT(unsupported_real_, function)(p1, p2, p3, p4, p5, p6);         \
  }

#undef HookWrapper7
#define HookWrapper7(ret, function, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6, p6, t7, p7)    \
  typedef ret(WINAPI *CONCAT(function, _hooktype))(t1, t2, t3, t4, t5, t6, t7);                \
  CONCAT(function, _hooktype) CONCAT(unsupported_real_, function);                             \
  static ret WINAPI CONCAT(function, _hooked)(t1 p1, t2 p2, t3 p3, t4 p4, t5 p5, t6 p6, t7 p7) \
  {                                                                                            \
    static bool hit = false;                                                                   \
    if(hit == false)                                                                           \
    {                                                                                          \
      RDCERR("Function " STRINGIZE(function) " not supported - capture may be broken");        \
      hit = true;                                                                              \
    }                                                                                          \
    return glhooks.CONCAT(unsupported_real_, function)(p1, p2, p3, p4, p5, p6, p7);            \
  }

#undef HookWrapper8
#define HookWrapper8(ret, function, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6, p6, t7, p7, t8, p8)   \
  typedef ret(WINAPI *CONCAT(function, _hooktype))(t1, t2, t3, t4, t5, t6, t7, t8);                   \
  CONCAT(function, _hooktype) CONCAT(unsupported_real_, function);                                    \
  static ret WINAPI CONCAT(function, _hooked)(t1 p1, t2 p2, t3 p3, t4 p4, t5 p5, t6 p6, t7 p7, t8 p8) \
  {                                                                                                   \
    static bool hit = false;                                                                          \
    if(hit == false)                                                                                  \
    {                                                                                                 \
      RDCERR("Function " STRINGIZE(function) " not supported - capture may be broken");               \
      hit = true;                                                                                     \
    }                                                                                                 \
    return glhooks.CONCAT(unsupported_real_, function)(p1, p2, p3, p4, p5, p6, p7, p8);               \
  }

#undef HookWrapper9
#define HookWrapper9(ret, function, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6, p6, t7, p7, t8, \
                     p8, t9, p9)                                                                \
  typedef ret(WINAPI *CONCAT(function, _hooktype))(t1, t2, t3, t4, t5, t6, t7, t8, t9);         \
  CONCAT(function, _hooktype) CONCAT(unsupported_real_, function);                              \
  static ret WINAPI CONCAT(function, _hooked)(t1 p1, t2 p2, t3 p3, t4 p4, t5 p5, t6 p6, t7 p7,  \
                                              t8 p8, t9 p9)                                     \
  {                                                                                             \
    static bool hit = false;                                                                    \
    if(hit == false)                                                                            \
    {                                                                                           \
      RDCERR("Function " STRINGIZE(function) " not supported - capture may be broken");         \
      hit = true;                                                                               \
    }                                                                                           \
    return glhooks.CONCAT(unsupported_real_, function)(p1, p2, p3, p4, p5, p6, p7, p8, p9);     \
  }

#undef HookWrapper10
#define HookWrapper10(ret, function, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6, p6, t7, p7, t8, \
                      p8, t9, p9, t10, p10)                                                      \
  typedef ret(WINAPI *CONCAT(function, _hooktype))(t1, t2, t3, t4, t5, t6, t7, t8, t9, t10);     \
  CONCAT(function, _hooktype) CONCAT(unsupported_real_, function);                               \
  static ret WINAPI CONCAT(function, _hooked)(t1 p1, t2 p2, t3 p3, t4 p4, t5 p5, t6 p6, t7 p7,   \
                                              t8 p8, t9 p9, t10 p10)                             \
  {                                                                                              \
    static bool hit = false;                                                                     \
    if(hit == false)                                                                             \
    {                                                                                            \
      RDCERR("Function " STRINGIZE(function) " not supported - capture may be broken");          \
      hit = true;                                                                                \
    }                                                                                            \
    return glhooks.CONCAT(unsupported_real_, function)(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10); \
  }

#undef HookWrapper11
#define HookWrapper11(ret, function, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6, p6, t7, p7, t8,  \
                      p8, t9, p9, t10, p10, t11, p11)                                             \
  typedef ret(WINAPI *CONCAT(function, _hooktype))(t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11); \
  CONCAT(function, _hooktype) CONCAT(unsupported_real_, function);                                \
  static ret WINAPI CONCAT(function, _hooked)(t1 p1, t2 p2, t3 p3, t4 p4, t5 p5, t6 p6, t7 p7,    \
                                              t8 p8, t9 p9, t10 p10, t11 p11)                     \
  {                                                                                               \
    static bool hit = false;                                                                      \
    if(hit == false)                                                                              \
    {                                                                                             \
      RDCERR("Function " STRINGIZE(function) " not supported - capture may be broken");           \
      hit = true;                                                                                 \
    }                                                                                             \
    return glhooks.CONCAT(unsupported_real_, function)(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10,   \
                                                       p11);                                      \
  }

#undef HookWrapper12
#define HookWrapper12(ret, function, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6, p6, t7, p7, t8, \
                      p8, t9, p9, t10, p10, t11, p11, t12, p12)                                  \
  typedef ret(WINAPI *CONCAT(function, _hooktype))(t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11, \
                                                   t12);                                         \
  CONCAT(function, _hooktype) CONCAT(unsupported_real_, function);                               \
  static ret WINAPI CONCAT(function, _hooked)(t1 p1, t2 p2, t3 p3, t4 p4, t5 p5, t6 p6, t7 p7,   \
                                              t8 p8, t9 p9, t10 p10, t11 p11, t12 p12)           \
  {                                                                                              \
    static bool hit = false;                                                                     \
    if(hit == false)                                                                             \
    {                                                                                            \
      RDCERR("Function " STRINGIZE(function) " not supported - capture may be broken");          \
      hit = true;                                                                                \
    }                                                                                            \
    return glhooks.CONCAT(unsupported_real_, function)(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10,  \
                                                       p11, p12);                                \
  }

#undef HookWrapper13
#define HookWrapper13(ret, function, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6, p6, t7, p7, t8, \
                      p8, t9, p9, t10, p10, t11, p11, t12, p12, t13, p13)                        \
  typedef ret(WINAPI *CONCAT(function, _hooktype))(t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11, \
                                                   t12, t13);                                    \
  CONCAT(function, _hooktype) CONCAT(unsupported_real_, function);                               \
  static ret WINAPI CONCAT(function, _hooked)(t1 p1, t2 p2, t3 p3, t4 p4, t5 p5, t6 p6, t7 p7,   \
                                              t8 p8, t9 p9, t10 p10, t11 p11, t12 p12, t13 p13)  \
  {                                                                                              \
    static bool hit = false;                                                                     \
    if(hit == false)                                                                             \
    {                                                                                            \
      RDCERR("Function " STRINGIZE(function) " not supported - capture may be broken");          \
      hit = true;                                                                                \
    }                                                                                            \
    return glhooks.CONCAT(unsupported_real_, function)(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10,  \
                                                       p11, p12, p13);                           \
  }

#undef HookWrapper14
#define HookWrapper14(ret, function, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6, p6, t7, p7, t8, \
                      p8, t9, p9, t10, p10, t11, p11, t12, p12, t13, p13, t14, p14)              \
  typedef ret(WINAPI *CONCAT(function, _hooktype))(t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11, \
                                                   t12, t13, t14);                               \
  CONCAT(function, _hooktype) CONCAT(unsupported_real_, function);                               \
  static ret WINAPI CONCAT(function, _hooked)(t1 p1, t2 p2, t3 p3, t4 p4, t5 p5, t6 p6, t7 p7,   \
                                              t8 p8, t9 p9, t10 p10, t11 p11, t12 p12, t13 p13,  \
                                              t14 p14)                                           \
  {                                                                                              \
    static bool hit = false;                                                                     \
    if(hit == false)                                                                             \
    {                                                                                            \
      RDCERR("Function " STRINGIZE(function) " not supported - capture may be broken");          \
      hit = true;                                                                                \
    }                                                                                            \
    return glhooks.CONCAT(unsupported_real_, function)(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10,  \
                                                       p11, p12, p13, p14);                      \
  }

#undef HookWrapper15
#define HookWrapper15(ret, function, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6, p6, t7, p7, t8, \
                      p8, t9, p9, t10, p10, t11, p11, t12, p12, t13, p13, t14, p14, t15, p15)    \
  typedef ret(WINAPI *CONCAT(function, _hooktype))(t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11, \
                                                   t12, t13, t14, t15);                          \
  CONCAT(function, _hooktype) CONCAT(unsupported_real_, function);                               \
  static ret WINAPI CONCAT(function, _hooked)(t1 p1, t2 p2, t3 p3, t4 p4, t5 p5, t6 p6, t7 p7,   \
                                              t8 p8, t9 p9, t10 p10, t11 p11, t12 p12, t13 p13,  \
                                              t14 p14, t15 p15)                                  \
  {                                                                                              \
    static bool hit = false;                                                                     \
    if(hit == false)                                                                             \
    {                                                                                            \
      RDCERR("Function " STRINGIZE(function) " not supported - capture may be broken");          \
      hit = true;                                                                                \
    }                                                                                            \
    return glhooks.CONCAT(unsupported_real_, function)(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10,  \
                                                       p11, p12, p13, p14, p15);                 \
  }

#undef HookWrapper16
#define HookWrapper16(ret, function, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6, p6, t7, p7, t8,   \
                      p8, t9, p9, t10, p10, t11, p11, t12, p12, t13, p13, t14, p14, t15, p15, t16, \
                      p16)                                                                         \
  typedef ret(WINAPI *CONCAT(function, _hooktype))(t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11,   \
                                                   t12, t13, t14, t15, t16);                       \
  CONCAT(function, _hooktype) CONCAT(unsupported_real_, function);                                 \
  static ret WINAPI CONCAT(function, _hooked)(t1 p1, t2 p2, t3 p3, t4 p4, t5 p5, t6 p6, t7 p7,     \
                                              t8 p8, t9 p9, t10 p10, t11 p11, t12 p12, t13 p13,    \
                                              t14 p14, t15 p15, t16 p16)                           \
  {                                                                                                \
    static bool hit = false;                                                                       \
    if(hit == false)                                                                               \
    {                                                                                              \
      RDCERR("Function " STRINGIZE(function) " not supported - capture may be broken");            \
      hit = true;                                                                                  \
    }                                                                                              \
    return glhooks.CONCAT(unsupported_real_, function)(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10,    \
                                                       p11, p12, p13, p14, p15, p16);              \
  }

#undef HookWrapper17
#define HookWrapper17(ret, function, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6, p6, t7, p7, t8,   \
                      p8, t9, p9, t10, p10, t11, p11, t12, p12, t13, p13, t14, p14, t15, p15, t16, \
                      p16, t17, p17)                                                               \
  typedef ret(WINAPI *CONCAT(function, _hooktype))(t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11,   \
                                                   t12, t13, t14, t15, t16, t17);                  \
  CONCAT(function, _hooktype) CONCAT(unsupported_real_, function);                                 \
  static ret WINAPI CONCAT(function, _hooked)(t1 p1, t2 p2, t3 p3, t4 p4, t5 p5, t6 p6, t7 p7,     \
                                              t8 p8, t9 p9, t10 p10, t11 p11, t12 p12, t13 p13,    \
                                              t14 p14, t15 p15, t16 p16, t17 p17)                  \
  {                                                                                                \
    static bool hit = false;                                                                       \
    if(hit == false)                                                                               \
    {                                                                                              \
      RDCERR("Function " STRINGIZE(function) " not supported - capture may be broken");            \
      hit = true;                                                                                  \
    }                                                                                              \
    return glhooks.CONCAT(unsupported_real_, function)(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10,    \
                                                       p11, p12, p13, p14, p15, p16, p17);         \
  }

  DefineUnsupportedDummies();

  /*
         in bash:

      function HookWrapper()
      {
          N=$1;
          echo "#undef HookWrapper$N";
          echo -n "#define HookWrapper$N(ret, function";
              for I in `seq 1 $N`; do echo -n ", t$I, p$I"; done;
          echo ") \\";

          echo -e "\tCONCAT(unsupported_real_,function) = NULL;";
      }

    for I in `seq 0 17`; do HookWrapper $I; echo; done
  */
  void SetUnsupportedFunctionPointersToNULL()
  {
#undef HookWrapper0
#define HookWrapper0(ret, function) CONCAT(unsupported_real_, function) = NULL;

#undef HookWrapper1
#define HookWrapper1(ret, function, t1, p1) CONCAT(unsupported_real_, function) = NULL;

#undef HookWrapper2
#define HookWrapper2(ret, function, t1, p1, t2, p2) CONCAT(unsupported_real_, function) = NULL;

#undef HookWrapper3
#define HookWrapper3(ret, function, t1, p1, t2, p2, t3, p3) \
  CONCAT(unsupported_real_, function) = NULL;

#undef HookWrapper4
#define HookWrapper4(ret, function, t1, p1, t2, p2, t3, p3, t4, p4) \
  CONCAT(unsupported_real_, function) = NULL;

#undef HookWrapper5
#define HookWrapper5(ret, function, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5) \
  CONCAT(unsupported_real_, function) = NULL;

#undef HookWrapper6
#define HookWrapper6(ret, function, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6, p6) \
  CONCAT(unsupported_real_, function) = NULL;

#undef HookWrapper7
#define HookWrapper7(ret, function, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6, p6, t7, p7) \
  CONCAT(unsupported_real_, function) = NULL;

#undef HookWrapper8
#define HookWrapper8(ret, function, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6, p6, t7, p7, t8, p8) \
  CONCAT(unsupported_real_, function) = NULL;

#undef HookWrapper9
#define HookWrapper9(ret, function, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6, p6, t7, p7, t8, \
                     p8, t9, p9)                                                                \
  CONCAT(unsupported_real_, function) = NULL;

#undef HookWrapper10
#define HookWrapper10(ret, function, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6, p6, t7, p7, t8, \
                      p8, t9, p9, t10, p10)                                                      \
  CONCAT(unsupported_real_, function) = NULL;

#undef HookWrapper11
#define HookWrapper11(ret, function, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6, p6, t7, p7, t8, \
                      p8, t9, p9, t10, p10, t11, p11)                                            \
  CONCAT(unsupported_real_, function) = NULL;

#undef HookWrapper12
#define HookWrapper12(ret, function, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6, p6, t7, p7, t8, \
                      p8, t9, p9, t10, p10, t11, p11, t12, p12)                                  \
  CONCAT(unsupported_real_, function) = NULL;

#undef HookWrapper13
#define HookWrapper13(ret, function, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6, p6, t7, p7, t8, \
                      p8, t9, p9, t10, p10, t11, p11, t12, p12, t13, p13)                        \
  CONCAT(unsupported_real_, function) = NULL;

#undef HookWrapper14
#define HookWrapper14(ret, function, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6, p6, t7, p7, t8, \
                      p8, t9, p9, t10, p10, t11, p11, t12, p12, t13, p13, t14, p14)              \
  CONCAT(unsupported_real_, function) = NULL;

#undef HookWrapper15
#define HookWrapper15(ret, function, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6, p6, t7, p7, t8, \
                      p8, t9, p9, t10, p10, t11, p11, t12, p12, t13, p13, t14, p14, t15, p15)    \
  CONCAT(unsupported_real_, function) = NULL;

#undef HookWrapper16
#define HookWrapper16(ret, function, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6, p6, t7, p7, t8,   \
                      p8, t9, p9, t10, p10, t11, p11, t12, p12, t13, p13, t14, p14, t15, p15, t16, \
                      p16)                                                                         \
  CONCAT(unsupported_real_, function) = NULL;

#undef HookWrapper17
#define HookWrapper17(ret, function, t1, p1, t2, p2, t3, p3, t4, p4, t5, p5, t6, p6, t7, p7, t8,   \
                      p8, t9, p9, t10, p10, t11, p11, t12, p12, t13, p13, t14, p14, t15, p15, t16, \
                      p16, t17, p17)                                                               \
  CONCAT(unsupported_real_, function) = NULL;

    DefineUnsupportedDummies();
  }
};

OpenGLHook OpenGLHook::glhooks;

void PopulateGLFunctions()
{
  OpenGLHook::glhooks.PopulateGLFunctions();
}

GLPlatform &GetGLPlatform()
{
  return OpenGLHook::glhooks;
}

// dirty immediate mode rendering functions for backwards compatible
// rendering of overlay text
typedef void(WINAPI *GLGETINTEGERVPROC)(GLenum, GLint *);
typedef void(WINAPI *GLPUSHMATRIXPROC)();
typedef void(WINAPI *GLLOADIDENTITYPROC)();
typedef void(WINAPI *GLMATRIXMODEPROC)(GLenum);
typedef void(WINAPI *GLORTHOPROC)(GLdouble, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble);
typedef void(WINAPI *GLPOPMATRIXPROC)();
typedef void(WINAPI *GLBEGINPROC)(GLenum);
typedef void(WINAPI *GLVERTEX2FPROC)(float, float);
typedef void(WINAPI *GLTEXCOORD2FPROC)(float, float);
typedef void(WINAPI *GLENDPROC)();

static GLGETINTEGERVPROC getInt = NULL;
static GLPUSHMATRIXPROC pushm = NULL;
static GLLOADIDENTITYPROC loadident = NULL;
static GLMATRIXMODEPROC matMode = NULL;
static GLORTHOPROC ortho = NULL;
static GLPOPMATRIXPROC popm = NULL;
static GLBEGINPROC begin = NULL;
static GLVERTEX2FPROC v2f = NULL;
static GLTEXCOORD2FPROC t2f = NULL;
static GLENDPROC end = NULL;

const GLenum MAT_MODE = (GLenum)0x0BA0;
const GLenum MAT_MDVW = (GLenum)0x1700;
const GLenum MAT_PROJ = (GLenum)0x1701;

static bool immediateInited = false;

bool OpenGLHook::DrawQuads(float width, float height, const std::vector<Vec4f> &vertices)
{
  if(!immediateInited)
  {
    HMODULE mod = GetModuleHandleA("opengl32.dll");

    if(mod == NULL)
      return false;

    getInt = (GLGETINTEGERVPROC)GetProcAddress(mod, "glGetIntegerv");
    if(!getInt)
      return false;
    pushm = (GLPUSHMATRIXPROC)GetProcAddress(mod, "glPushMatrix");
    if(!pushm)
      return false;
    loadident = (GLLOADIDENTITYPROC)GetProcAddress(mod, "glLoadIdentity");
    if(!loadident)
      return false;
    matMode = (GLMATRIXMODEPROC)GetProcAddress(mod, "glMatrixMode");
    if(!matMode)
      return false;
    ortho = (GLORTHOPROC)GetProcAddress(mod, "glOrtho");
    if(!ortho)
      return false;
    popm = (GLPOPMATRIXPROC)GetProcAddress(mod, "glPopMatrix");
    if(!popm)
      return false;
    begin = (GLBEGINPROC)GetProcAddress(mod, "glBegin");
    if(!begin)
      return false;
    v2f = (GLVERTEX2FPROC)GetProcAddress(mod, "glVertex2f");
    if(!v2f)
      return false;
    t2f = (GLTEXCOORD2FPROC)GetProcAddress(mod, "glTexCoord2f");
    if(!t2f)
      return false;
    end = (GLENDPROC)GetProcAddress(mod, "glEnd");
    if(!end)
      return false;

    immediateInited = true;
  }

  GLenum prevMatMode = eGL_NONE;
  getInt(MAT_MODE, (GLint *)&prevMatMode);

  matMode(MAT_PROJ);
  pushm();
  loadident();
  ortho(0.0, width, height, 0.0, -1.0, 1.0);

  matMode(MAT_MDVW);
  pushm();
  loadident();

  matMode(prevMatMode);

  begin(eGL_QUADS);

  for(size_t i = 0; i < vertices.size(); i++)
  {
    t2f(vertices[i].z, vertices[i].w);
    v2f(vertices[i].x, vertices[i].y);
  }

  end();

  getInt(MAT_MODE, (GLint *)&prevMatMode);

  matMode(MAT_PROJ);
  popm();
  matMode(MAT_MDVW);
  popm();

  matMode(prevMatMode);

  return true;
}
