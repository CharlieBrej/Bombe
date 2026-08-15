AC_INIT([Bombe], [0.0.1])
AM_INIT_AUTOMAKE([foreign])
AC_LANG(C++)
AC_PROG_CXX
AC_CONFIG_FILES([Makefile])
AC_CANONICAL_HOST
AC_PROG_OBJCXX

PKG_CHECK_MODULES([SDL2], [sdl2])
PKG_CHECK_MODULES([SDL2_image], [SDL2_image])
PKG_CHECK_MODULES([SDL2_mixer], [SDL2_mixer])
PKG_CHECK_MODULES([SDL2_net], [SDL2_net])
PKG_CHECK_MODULES([SDL2_ttf], [SDL2_ttf])
PKG_CHECK_MODULES([Z3], [z3])
PKG_CHECK_MODULES([ZSTD], [libzstd])
PKG_CHECK_MODULES([libcurl], [libcurl])

build_linux=no
build_mac=no

# Detect the target system
case "${host_os}" in
    linux*)
        build_linux=yes
        PKG_CHECK_MODULES([XCB], [xcb])
        ;;
    darwin*)
        build_mac=yes
        ;;
    *)
        AC_MSG_ERROR(["OS $host_os is not supported"])
        ;;
esac

AC_ARG_ENABLE([steam],
[  --disable-steam    Turn off Steam],
[case "${enableval}" in
  yes) steam=true ;;
  no)  steam=false ;;
  *) AC_MSG_ERROR([bad value ${enableval} for --enable-steam]) ;;
esac],[steam=true])

AC_ARG_ENABLE([control-socket],
    AS_HELP_STRING([--enable-control-socket],
                   [build the local Unix control socket and BombeControl client (default=no)]),
    [case "${enableval}" in
      yes) control_socket=true ;;
      no)  control_socket=false ;;
      *) AC_MSG_ERROR([bad value ${enableval} for --enable-control-socket]) ;;
    esac],
    [control_socket=false])

AS_IF([test x$control_socket = xtrue], [
    AS_CASE([$host_os],
        [linux*|darwin*], [],
        [AC_MSG_ERROR([--enable-control-socket is only supported on Unix hosts])])
    AC_CHECK_HEADERS([sys/socket.h sys/un.h unistd.h], [],
                     [AC_MSG_ERROR([Unix-domain socket headers are required])])
    AC_CHECK_FUNCS([socket bind listen accept close unlink], [],
                   [AC_MSG_ERROR([Unix-domain socket APIs are required])])
    AC_DEFINE([ENABLE_CONTROL_SOCKET], [1],
              [Define to build the local Unix control socket])
])

AC_ARG_WITH(secret-id,
            AS_HELP_STRING([--with-secret-id=N],
                           [Secret server ID (default=0)]),
            [], [with_secret_id=0])



AM_CONDITIONAL([NO_STEAM], [test x$steam = xfalse])
AM_CONDITIONAL([CONTROL_SOCKET], [test x$control_socket = xtrue])
AM_CONDITIONAL([LINUX], [test "$build_linux" = "yes"])
AM_CONDITIONAL([MACOS], [test "$build_mac" = "yes"])
AC_DEFINE_UNQUOTED([SECRET_ID], [$with_secret_id])

AC_OUTPUT
