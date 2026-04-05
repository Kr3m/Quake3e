/* config.h for libneon 0.33.0 – MinGW-w64 / Windows cross-compile
 * Hand-crafted for Quake3e; no GnuTLS (HTTPS disabled on Windows). */

/* #undef AC_APPLE_UNIVERSAL_BUILD */
/* #undef EGD_PATH */
/* #undef ENABLE_EGD */
/* #undef GSS_C_NT_HOSTBASED_SERVICE */

/* MinGW-w64 provides these via winsock2/ws2tcpip */
/* #undef HAVE_ARPA_INET_H */
/* #undef HAVE_BIND_TEXTDOMAIN_CODESET */
/* #undef HAVE_CRYPTO_SET_IDPTR_CALLBACK */
/* #undef HAVE_DECL_H_ERRNO */

#define HAVE_DECL_STPCPY 0
#define HAVE_DECL_STRERROR_R 0

/* #undef HAVE_DLFCN_H */
#define HAVE_ERRNO_H 1
/* #undef HAVE_EXPAT */
/* #undef HAVE_EXPLICIT_BZERO */
/* #undef HAVE_FCNTL */
/* #undef HAVE_FCNTL_H */
/* #undef HAVE_FSTAT64 */
#define HAVE_GAI_STRERROR 1
#define HAVE_GETHOSTNAME 1
#define HAVE_GETNAMEINFO 1
#define HAVE_GETSOCKOPT 1
#define HAVE_GETTIMEOFDAY 1
#define HAVE_GMTIME_R 1

/* No GnuTLS on Windows */
/* #undef HAVE_GNUTLS */
/* #undef HAVE_GNUTLS_CERTIFICATE_GET_ISSUER */
/* #undef HAVE_GNUTLS_CERTIFICATE_GET_X509_CAS */
/* #undef HAVE_GNUTLS_CERTIFICATE_SET_RETRIEVE_FUNCTION2 */
/* #undef HAVE_GNUTLS_CERTIFICATE_SET_X509_SYSTEM_TRUST */
/* #undef HAVE_GNUTLS_PRIVKEY_IMPORT_EXT */
/* #undef HAVE_GNUTLS_SESSION_GET_DATA2 */
/* #undef HAVE_GNUTLS_X509_CRT_EQUALS */
/* #undef HAVE_GNUTLS_X509_CRT_SIGN2 */
/* #undef HAVE_GNUTLS_X509_DN_GET_RDN_AVA */

/* #undef HAVE_GSSAPI */
/* #undef HAVE_GSSAPI_GSSAPI_GENERIC_H */
/* #undef HAVE_GSSAPI_GSSAPI_H */
/* #undef HAVE_GSSAPI_H */
/* #undef HAVE_GSS_INIT_SEC_CONTEXT */
/* #undef HAVE_HSTRERROR */
/* #undef HAVE_ICONV */
/* #undef HAVE_ICONV_H */
#define HAVE_INET_NTOP 1
#define HAVE_INET_PTON 1
#define HAVE_INTTYPES_H 1
/* #undef HAVE_ISATTY */
/* #undef HAVE_LIBINTL_H */
/* #undef HAVE_LIBPROXY */
/* #undef HAVE_LIBXML */
/* #undef HAVE_LIBXML_PARSER_H */
/* #undef HAVE_LIBXML_XMLVERSION_H */
#define HAVE_LIMITS_H 1
/* #undef HAVE_LOCALE_H */
/* #undef HAVE_LSEEK64 */
/* #undef HAVE_MINIX_CONFIG_H */
/* #undef HAVE_NETDB_H */
/* #undef HAVE_NETINET_IN_H */
/* #undef HAVE_NETINET_TCP_H */
/* #undef HAVE_NTLM */
/* #undef HAVE_OPENSSL */
/* #undef HAVE_OPENSSL11 */
/* #undef HAVE_OPENSSL_OPENSSLV_H */
/* #undef HAVE_OPENSSL_SSL_H */
/* #undef HAVE_PAKCHOIS */
/* #undef HAVE_PIPE */
/* #undef HAVE_POLL */
/* #undef HAVE_PTHREAD_MUTEX_INIT */
/* #undef HAVE_PTHREAD_MUTEX_LOCK */
/* #undef HAVE_SENDMSG */
/* #undef HAVE_SETLOCALE */
#define HAVE_SETSOCKOPT 1
/* #undef HAVE_SETVBUF */
#define HAVE_SHUTDOWN 1
/* #undef HAVE_SIGNAL */
/* #undef HAVE_SIGNAL_H */
#define HAVE_SNPRINTF 1
#define HAVE_SOCKLEN_T 1
/* #undef HAVE_SSL_SESSION_CMP */
#define HAVE_STDARG_H 1
#define HAVE_STDINT_H 1
#define HAVE_STDIO_H 1
#define HAVE_STDLIB_H 1
/* MinGW-w64 lacks stpcpy */
/* #undef HAVE_STPCPY */
#define HAVE_STRCASECMP 1
/* #undef HAVE_STRERROR_R */
#define HAVE_STRINGS_H 1
#define HAVE_STRING_H 1
#define HAVE_STRTOLL 1
/* #undef HAVE_STRTOQ */
/* #undef HAVE_STRUCT_TM_TM_GMTOFF */
/* #undef HAVE_STRUCT_TM___TM_GMTOFF */
/* #undef HAVE_SYS_LIMITS_H */
/* #undef HAVE_SYS_POLL_H */
/* #undef HAVE_SYS_SELECT_H */
/* #undef HAVE_SYS_SOCKET_H */
#define HAVE_SYS_STAT_H 1
/* #undef HAVE_SYS_TIME_H */
#define HAVE_SYS_TYPES_H 1
/* #undef HAVE_SYS_UIO_H */
/* #undef HAVE_TIMEZONE */
/* #undef HAVE_TRIO */
/* #undef HAVE_TRIO_H */
/* #undef HAVE_UNISTD_H */
/* #undef HAVE_USLEEP */
#define HAVE_VSNPRINTF 1
#define HAVE_WCHAR_H 1
/* #undef HAVE_WSPIAPI_H */

/* #undef LOCALEDIR */
#define LT_OBJDIR ".libs/"
#define NEON_IS_LIBRARY 1
#define NEON_VERSION "0.33.0"
#define NE_DEBUGGING 1
/* #undef NE_ENABLE_AUTO_LIBPROXY */
#define NE_FMT_NE_OFF_T NE_FMT_OFF_T
/* #undef NE_FMT_OFF64_T */
#define NE_FMT_OFF_T "ld"
#define NE_FMT_SIZE_T "lu"
#define NE_FMT_SSIZE_T "ld"
#define NE_FMT_TIME_T "ld"
/* #undef NE_FMT_XML_SIZE */
/* #undef NE_HAVE_DAV */
/* #undef NE_HAVE_GSSAPI */
/* #undef NE_HAVE_I18N */
#define NE_HAVE_IPV6 1
/* #undef NE_HAVE_LFS */
/* #undef NE_HAVE_LIBPXY */
/* No SSL on Windows build */
/* #undef NE_HAVE_SSL */
/* #undef NE_HAVE_TS_SSL */
/* #undef NE_HAVE_ZLIB */
/* #undef NE_SSL_CA_BUNDLE */
/* Use select() not poll() on Windows */
/* #undef NE_USE_POLL */

#define NE_VERSION_MAJOR (0)
#define NE_VERSION_MINOR (33)
#define NE_VERSION_PATCH (0)

#define PACKAGE_BUGREPORT ""
#define PACKAGE_NAME "neon"
#define PACKAGE_STRING "neon 0.33.0"
#define PACKAGE_TARNAME "neon"
#define PACKAGE_URL "https://notroj.github.io/neon/"
#define PACKAGE_VERSION "0.33.0"

#define SIZEOF_INT 4
#define SIZEOF_LONG 4   /* 32-bit on Windows even in x86_64 */
#define SIZEOF_LONG_LONG 8
/* #undef SIZEOF_OFF64_T */
#define SIZEOF_OFF_T 8
#define SIZEOF_SIZE_T 8
#define SIZEOF_SSIZE_T 8
#define SIZEOF_TIME_T 8
/* #undef SIZEOF_XML_SIZE */

#define STDC_HEADERS 1
/* #undef STRERROR_R_CHAR_P */
