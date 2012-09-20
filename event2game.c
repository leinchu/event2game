/*
  +----------------------------------------------------------------------+
  | PHP Version 5                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2012 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author: Except you to join  ^ ^                                                           |  
  |             Lein <leinurg@gmail.com>                                                             |  
  +----------------------------------------------------------------------+
*/

/* $Id$ */
/* $Id: event2game.c ? 2012-08-29 16:19:37  $ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "php_event2game.h"

#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <signal.h>
#ifndef WIN32
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/util.h>
#include <event2/event.h>

static int custom_thread_count = 0;
static void listener_cb(struct evconnlistener *, evutil_socket_t, struct sockaddr *, int socklen, void *);
static void conn_eventcb(struct bufferevent *, short, void *);
static void signal_cb(evutil_socket_t, short, void *);
static void conn_readcb(struct bufferevent *incoming,void *arg);
static void event_thread(void *args);
static void custom_thread(void *args);
static void _le_bufferevent_dtor(zend_rsrc_list_entry *rsrc TSRMLS_DC);
static char *getNameValue(char *name,char *value,int *l);
static char *makeHeader(int type, int requestId, int contentLength, int paddingLength);
static char *makeNameValueHeader(char *name, char *value);
static int evtgame_Log(char * filename, const char *fmt, ...);

typedef struct {
	int *status;
	zval *runable_fun;
} custom_fun_arg;

zval *read_callback,*open_callback,*close_callback;
#define max_incoming_len 2048 //最大允许用户上行数据长度
#define pt_fl printf("%s -> %d\n", __FUNCTION__, __LINE__)
#define HEATBEAT_MSG "HB"
#define MAXBUF 2048
#define BUFFER_CGI_CLI_SIZE 51200
#define FCGI_VERSION_1 1
#define PHP_EVENT2GAME_VERSION 0.1

static char *policyXML="<cross-domain-policy><allow-access-from domain=\"*\" to-ports=\"*\"/></cross-domain-policy>";
static char *policeRequestStr="<policy-file-request/>";
static int policyXMLLen = 0;

static char *cgi_file_fullpath = NULL;
static long pport=8080, precv_timeout=15, psend_timeout=15;

/* If you declare any globals in php_event2game.h uncomment this:
ZEND_DECLARE_MODULE_GLOBALS(event2game)
*/

/* True global resources - no need for thread safety here */
int le_bufferevent;
#define le_bufferevent_name  "event2game buffer event"
#if (PHP_MAJOR_VERSION == 5 && PHP_MINOR_VERSION >= 3) || PHP_MAJOR_VERSION > 5
# define EVENT_ARGINFO
#else
# define EVENT_ARGINFO static
#endif

EVENT_ARGINFO
ZEND_BEGIN_ARG_INFO_EX(arginfo_evtgame_set_function, 0, 0, 3)
	ZEND_ARG_INFO(1, open_callback)
	ZEND_ARG_INFO(1, close_callback)
	ZEND_ARG_INFO(1, read_callback)
ZEND_END_ARG_INFO()

EVENT_ARGINFO
ZEND_BEGIN_ARG_INFO_EX(arginfo_evtgame_run, 0, 0, 0)
	ZEND_ARG_INFO(0, port)
	ZEND_ARG_INFO(0, recv_timeout)
	ZEND_ARG_INFO(0, send_timeout)
ZEND_END_ARG_INFO()

EVENT_ARGINFO
ZEND_BEGIN_ARG_INFO_EX(arginfo_evtgame_send, 0, 0, 2)
	ZEND_ARG_INFO(0, rsrc)
	ZEND_ARG_INFO(0, message)
ZEND_END_ARG_INFO()
	
EVENT_ARGINFO
ZEND_BEGIN_ARG_INFO_EX(arginfo_evtgame_thread_start, 0, 0, 1)
	ZEND_ARG_INFO(1, func)
ZEND_END_ARG_INFO()

EVENT_ARGINFO
ZEND_BEGIN_ARG_INFO_EX(arginfo_evtgame_cgi_filepath, 0, 0, 1)
	ZEND_ARG_INFO(0, file_path)
ZEND_END_ARG_INFO()

EVENT_ARGINFO
ZEND_BEGIN_ARG_INFO_EX(arginfo_evtgame_cgi_request, 0, 0, 4)
	ZEND_ARG_INFO(0, session_id)
	ZEND_ARG_INFO(0, request_data)
	ZEND_ARG_INFO(0, server_ip)
	ZEND_ARG_INFO(0, server_port)
ZEND_END_ARG_INFO()

/* {{{ event2game_functions[]
 *
 * Every user visible function must have an entry in event2game_functions[].
 */
const zend_function_entry event2game_functions[] = {
	PHP_FE(evtgame_set_function,	arginfo_evtgame_set_function)
	PHP_FE(evtgame_run,				arginfo_evtgame_run)
	PHP_FE(evtgame_send,			arginfo_evtgame_send)
	PHP_FE(evtgame_thread_start,	arginfo_evtgame_thread_start)	
	PHP_FE(evtgame_cgi_request,		arginfo_evtgame_cgi_request)
	PHP_FE(evtgame_cgi_filepath, 	arginfo_evtgame_cgi_filepath)
	PHP_FE_END	/* Must be the last line in event2game_functions[] */
};
/* }}} */

/* {{{ event2game_module_entry
 */
zend_module_entry event2game_module_entry = {
#if ZEND_MODULE_API_NO >= 20010901
	STANDARD_MODULE_HEADER,
#endif
	"event2game",
	event2game_functions,
	PHP_MINIT(event2game),
	PHP_MSHUTDOWN(event2game),
	PHP_RINIT(event2game),
	PHP_RSHUTDOWN(event2game),	
	PHP_MINFO(event2game),
#if ZEND_MODULE_API_NO >= 20010901
	"0.1", /* Replace with version number for your extension */
#endif
	STANDARD_MODULE_PROPERTIES
};
/* }}} */

#ifdef COMPILE_DL_EVENT2GAME
ZEND_GET_MODULE(event2game)
#endif

/* {{{ PHP_INI
 */
/* Remove comments and fill if you need to have entries in php.ini
PHP_INI_BEGIN()
    STD_PHP_INI_ENTRY("event2game.global_value",      "42", PHP_INI_ALL, OnUpdateLong, global_value, zend_event2game_globals, event2game_globals)
    STD_PHP_INI_ENTRY("event2game.global_string", "foobar", PHP_INI_ALL, OnUpdateString, global_string, zend_event2game_globals, event2game_globals)
PHP_INI_END()
*/
/* }}} */

/* {{{ php_event2game_init_globals
 */
/* Uncomment this function if you have INI entries
static void php_event2game_init_globals(zend_event2game_globals *event2game_globals)
{
	event2game_globals->global_value = 0;
	event2game_globals->global_string = NULL;
}
*/
/* }}} */

/* {{{ PHP_MINIT_FUNCTION
 */
PHP_MINIT_FUNCTION(event2game)
{
	/* If you have INI entries, uncomment these lines 
	REGISTER_INI_ENTRIES();
	*/
	read_callback = NULL;
	open_callback = NULL;
	close_callback = NULL;
	policyXMLLen = strlen(policyXML);
	le_bufferevent = zend_register_list_destructors_ex(_le_bufferevent_dtor, NULL, le_bufferevent_name, module_number);
	REGISTER_STRING_CONSTANT("EVTGAME_HEATBEAT_MSG", HEATBEAT_MSG, CONST_CS | CONST_PERSISTENT);
	return SUCCESS;
}
/* }}} */

static void _le_bufferevent_dtor(zend_rsrc_list_entry *rsrc TSRMLS_DC) /* {{{ */
{
	//struct bufferevent *bev = (struct bufferevent*) rsrc->ptr;
	//if(bev!=NULL) bufferevent_free(bev);
}
/* }}} */


/* {{{ PHP_MSHUTDOWN_FUNCTION
 */
PHP_MSHUTDOWN_FUNCTION(event2game)
{
	/* uncomment this line if you have INI entries
	UNREGISTER_INI_ENTRIES();
	*/
	return SUCCESS;
}
/* }}} */

/* Remove if there's nothing to do at request start */
/* {{{ PHP_RINIT_FUNCTION
 */
PHP_RINIT_FUNCTION(event2game)
{
	return SUCCESS;
}
/* }}} */

/* Remove if there's nothing to do at request end */
/* {{{ PHP_RSHUTDOWN_FUNCTION
 */
PHP_RSHUTDOWN_FUNCTION(event2game)
{
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MINFO_FUNCTION
 */
PHP_MINFO_FUNCTION(event2game)
{
	php_info_print_table_start();
	php_info_print_table_header(2, "event2game support", "enabled");	
	php_info_print_table_row(2, "Requirement", "Libevent 2.0");
	php_info_print_table_row(2, "extension version", PHP_EVENT2GAME_VERSION);
	php_info_print_table_end();

	/* Remove comments if you have entries in php.ini
	DISPLAY_INI_ENTRIES();
	*/
}
/* }}} */

/**************************************************************************/
/***************************          php function         **************************/
/**************************************************************************/

PHP_FUNCTION(evtgame_set_function)
{
	zval *z_read_callback = NULL, *z_open_callback = NULL, *z_close_callback = NULL;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "zzz", &z_open_callback, &z_close_callback, &z_read_callback) != SUCCESS) {
		php_error_docref(NULL TSRMLS_CC, E_ERROR, "Need String: open callback function name, close callback function name, data comming callback!");
		RETURN_FALSE;
	}

	char *func_name_open,*func_name_close,*func_name_read;

	if (!zend_is_callable(z_open_callback, 0, &func_name_open TSRMLS_CC)) {
		php_error_docref(NULL TSRMLS_CC, E_ERROR, "'%s' is not a valid callback for open", func_name_open);
		efree(func_name_open);
		RETURN_FALSE;
	}

	if (!zend_is_callable(z_close_callback, 0, &func_name_close TSRMLS_CC)) {
		php_error_docref(NULL TSRMLS_CC, E_ERROR, "'%s' is not a valid callback for close", func_name_close);
		efree(func_name_close);
		RETURN_FALSE;
	}

	if (!zend_is_callable(z_read_callback, 0, &func_name_read TSRMLS_CC)) {
		php_error_docref(NULL TSRMLS_CC, E_ERROR, "'%s' is not a valid callback for read", func_name_read);
		efree(func_name_read);
		RETURN_FALSE;
	}

	zval_add_ref( &z_open_callback );
	zval_add_ref( &z_close_callback );
	zval_add_ref( &z_read_callback );
	
	read_callback = z_read_callback;
	open_callback = z_open_callback;
	close_callback = z_close_callback;

	efree(func_name_open);
	efree(func_name_close);
	efree(func_name_read);
	RETURN_TRUE;

}


PHP_FUNCTION(evtgame_run)
{
	if (read_callback==NULL){		
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "use evtgame_set_function(open_callback, close_callback, read_callback) first!");
		RETURN_FALSE;
	}

	long port=0, recv_timeout=0, send_timeout=0;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|lll", &port, &recv_timeout, &send_timeout) == SUCCESS)
	{
		if(port>1024) pport = port;
		if(recv_timeout>5) precv_timeout = recv_timeout;
		if(send_timeout>5) psend_timeout = send_timeout;
	}else{
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "usage: evtgame_run(port[, recv_timeout, send_timeout])");
		RETURN_FALSE;
	}

	printf("param = %d, %d, %d\n", port, recv_timeout, send_timeout);
	
	
	pthread_t *event_pthread;
	event_pthread = emalloc(sizeof(pthread_t));
	
	int j = 0;
	int rs = pthread_create(event_pthread, NULL, event_thread, (void *)&j);
	if(rs!=0)
	{
		php_error_docref(NULL TSRMLS_CC, E_ERROR, "Create reading thread failed");
		RETURN_FALSE;
	}
	while(j!=-1) usleep(20);
	RETURN_TRUE;
}


PHP_FUNCTION(evtgame_send)
{
	zval *rsrc_id=0;
	int size;
	char *data;
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rs", &rsrc_id, &data, &size) != SUCCESS) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Need resource id which give you in open_callback(rsrc_id, fd), and string to send!");
		RETURN_FALSE;
	}

	if(!size) RETURN_FALSE;
	struct bufferevent *bev;
	ZEND_FETCH_RESOURCE(bev, struct bufferevent *, &rsrc_id, -1, le_bufferevent_name, le_bufferevent);
	
	bufferevent_write(bev, data, size);
	RETURN_TRUE;
}


PHP_FUNCTION(evtgame_thread_start)
{
	zval *z_fun = NULL;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "z", &z_fun) != SUCCESS) {
		php_error_docref(NULL TSRMLS_CC, E_ERROR, "Need function name which you want to run in a new thread");
		RETURN_FALSE;
	}

	zval_add_ref(&z_fun);

	char *func_name;	
	if (!zend_is_callable(z_fun, 0, &func_name TSRMLS_CC)) {
		php_error_docref(NULL TSRMLS_CC, E_ERROR, "'%s' is not a valid custom php function", func_name);
		efree(func_name);
		RETURN_FALSE;
	}

	efree(func_name);	

	pthread_t *custom_pthread;
	custom_pthread = emalloc(sizeof(pthread_t));

	int j = 0;

	custom_fun_arg *arg;
	arg = emalloc(sizeof(custom_fun_arg));

	arg->runable_fun = z_fun;
	arg->status = &j;

	int rs = pthread_create(custom_pthread, NULL, custom_thread, (void *)arg);
	if(rs!=0)
	{
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Create reading thread failed");
		RETURN_FALSE;
	}
	while(j!=-1) usleep(20);

	RETURN_TRUE;
}


PHP_FUNCTION(evtgame_cgi_filepath)
{
	char *path;
	int path_len;
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &path, &path_len) == FAILURE)
	{
		RETURN_FALSE;
	}

	if(cgi_file_fullpath!=NULL)
	{
		efree( cgi_file_fullpath );
		cgi_file_fullpath = NULL;
	}
	
	if(path_len<1) RETURN_FALSE;
	
	cgi_file_fullpath = emalloc( path_len+1 );
	memcpy(cgi_file_fullpath, path, path_len);
	cgi_file_fullpath[path_len] = '\0';

	RETURN_TRUE;
}


PHP_FUNCTION(evtgame_cgi_request)
{
	if(cgi_file_fullpath==NULL)
	{
		zend_error(E_WARNING, "Use evtgame_cgi_filepath(php_file_absolute_full_path) first");
		RETURN_FALSE;		
	}

	long s_len, r_len, svr_len, server_port;
	char *session_id, *request_data, *server_ip;
	char rtn[BUFFER_CGI_CLI_SIZE]= {0};

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "sssl", &session_id, &s_len, &request_data, &r_len, &server_ip, &svr_len, &server_port) == FAILURE)
	{
		zend_error(E_WARNING, "Usage : evtgame_cgi_request(session_id, string_to_send, fastcgi_server_ip, fastcgi_server_port)");
		RETURN_FALSE;
	}

	int len;
	int sockfd;

	struct sockaddr_in dest;

	bzero(&dest, sizeof(dest));
	dest.sin_family = AF_INET;

	dest.sin_port = htons(server_port);
	dest.sin_addr.s_addr = inet_addr(server_ip);
	
	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
	    sprintf(rtn,"%cSocket create error: %s!",5,strerror(errno));
		RETURN_STRING(rtn, 1);
	}

	struct timeval rcvto,sndto;
	int tolen=sizeof(struct timeval);

	rcvto.tv_sec=5;
	rcvto.tv_usec=0;

	sndto.tv_sec=1;
	sndto.tv_usec=0;
	//send timeout
	setsockopt(sockfd,SOL_SOCKET,SO_SNDTIMEO,&sndto,tolen);	
	//recv timeout
	setsockopt(sockfd,SOL_SOCKET,SO_RCVTIMEO,&rcvto,tolen); 

	if (connect(sockfd, (struct sockaddr *) &dest, sizeof(dest)) != 0)
	{
        sprintf(rtn,"%cSocket connect error: %s,server addr:%s,port:%d!",5,strerror(errno), server_ip, server_port); 
		RETURN_STRING(rtn, 1);
	}

	int l=0;
	char *header=calloc(1,17);
	sprintf(header,"%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c",1,1,0,1,0,8,0,0,0,1,0,0,0,0,0,0);
	len = send(sockfd, header, 16, 0);	
	if (len < 0)
	{
		sprintf(rtn, "%cdo_cgi_request (0) Message '%s' failed to send! Error code is %d, error message is %s \n",5, header, errno, strerror(errno));
		close(sockfd);
		RETURN_STRING(rtn, 1);
	}
	free(header);

	char *data1=getNameValue("REQUEST_METHOD","GET",&l);
	len = send(sockfd, data1, l, 0);	
	if (len < 0)
	{
		sprintf(rtn, "%cdo_cgi_request (1 REQUEST_METHOD) Message '%s' failed to send! Error code is %d, error message is %s\n",5, data1, errno, strerror(errno));
		close(sockfd);
		RETURN_STRING(rtn, 1);
	}
	free(data1);

	char *data=getNameValue("SCRIPT_FILENAME",cgi_file_fullpath,&l);
	len = send(sockfd, data, l, 0);	
	if (len < 0)
	{
		sprintf(rtn, "%cdo_cgi_request (cgi_file_fullpath) Message '%s' failed to send! Error code is %d, error message is %s\n",5, data, errno, strerror(errno));
		close(sockfd);
		RETURN_STRING(rtn, 1);
	}
	free(data);

	data=getNameValue("session_id",session_id,&l);
	len = send(sockfd, data, l, 0);
	if (len < 0)
	{
		sprintf(rtn, "%cdo_cgi_request (session_id) Message '%s' failed to send! Error code is %d, error message is %s\n",5, data, errno, strerror(errno));
		close(sockfd);
		RETURN_STRING(rtn, 1);
    }
	free(data);	

	data=getNameValue("request_data",request_data,&l);
	len = send(sockfd, data, l, 0);
	if (len < 0)
	{
		sprintf(rtn, "%cdo_cgi_request (request_data) Message '%s' failed to send! Error code is %d, error message is %s\n",5, data, errno, strerror(errno));
		close(sockfd);
		RETURN_STRING(rtn, 1);
    }
	free(data);

	data = makeHeader(5, 1, 0, 0);
	len = send(sockfd, data, 8, 0);
	if (len < 0)
	{
		sprintf(rtn, "%cdo_cgi_request (3) Message '%s' failed to send! Error code is %d, error message is %s\n",5, data, errno, strerror(errno));
		close(sockfd);
		RETURN_STRING(rtn, 1);
	}
	free(data);

	int length=0;
	int retry=0,nread=0,recv_size=2048;  

	while(retry<250)
	{
		retry++;
		nread = recv(sockfd, rtn+length, recv_size, 0);	
		if(nread>0)
		{
			length+=nread;
		}else{
			if(errno == EAGAIN)
			{
				usleep(20000);
				continue;
			}
			close(sockfd);
			RETURN_STRING("Receive cgi response fail!", 1);
		}

		if(nread<recv_size)
		{
			break;
		}

		if(length+recv_size>BUFFER_CGI_CLI_SIZE)
		{
			break;
		}		
	}

	if(retry>1) evtgame_Log(NULL, "---> do_cgi_request recv retry = ", retry, "\n");
	close(sockfd);

	if(length<20)//似乎不会发生,php fpm挂掉的时候会出现
	{
		RETURN_STRING("CGI server internal error!", 1);
	}else{
		if(rtn[1]==7)//php错误
		{
			RETURN_STRING(rtn+8, 1);
		}else{
			int i=0;
			for(i=8;i<length-3;i++)//skip html header
			{
				if(rtn[i]==13&&rtn[i+2]==13)
				{
					break;
				}
			}

			i+=4;//skip 2 \r\n,which is end of http header

			RETURN_STRING(rtn+i, 1);
		}
	}
}

static int evtgame_Log(char * filename, const char *fmt, ...)
{
	va_list args;
	int i;
    char data[1024]= {0};
    char realname[256] = {0};
    
    if (NULL == filename)
        sprintf(realname, "/tmp/event2game_cgi_log");
    else
        sprintf(realname, filename);
    
	va_start(args, fmt);
	i=vsprintf(data,fmt,args);
	va_end(args);
	strcat(data,"\n");
	
	FILE *fp = fopen(realname,"a+");
	if (NULL == fp)
	    return 0;
	fwrite(data, sizeof(char), strlen(data),fp);   
	fclose(fp);
	
	return i;
}

/**************************************************************************/
/***************************       libevent function      **************************/
/**************************************************************************/
static void event_thread(void *args)
{
	int *idin=(int *) args;
	int idx=*idin;

	struct event_base *base;
	struct evconnlistener *listener;
	struct event *signal_event;

	struct sockaddr_in sin;
#ifdef WIN32
	WSADATA wsa_data;
	WSAStartup(0x0201, &wsa_data);
#endif

	base = event_base_new();
	if (!base) {
		fprintf(stderr, "Could not initialize libevent!\n");
		return;
	}

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(pport);

	listener = evconnlistener_new_bind(base, listener_cb, (void *)base,
	    LEV_OPT_REUSEABLE|LEV_OPT_CLOSE_ON_FREE, -1,
	    (struct sockaddr*)&sin,
	    sizeof(sin));

	if (!listener) {
		fprintf(stderr, "Could not create a listener!\n");
		return;
	}

	signal_event = evsignal_new(base, SIGINT, signal_cb, (void *)base);

	if (!signal_event || event_add(signal_event, NULL)<0) {
		fprintf(stderr, "Could not create/add a signal event!\n");
		return;
	}

	*idin = -1;
	event_base_dispatch(base);

	evconnlistener_free(listener);
	event_free(signal_event);
	event_base_free(base);
	
	return;
}


static void
listener_cb(struct evconnlistener *listener, evutil_socket_t fd,
    struct sockaddr *sa, int socklen, void *user_data)
{
	//pt_fl;
	struct event_base *base = user_data;
	struct bufferevent *bev;

	bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
	if (!bev) {
		fprintf(stderr, "Error constructing bufferevent!");
		event_base_loopbreak(base);
		return;
	}
	bufferevent_setcb(bev, conn_readcb, NULL, conn_eventcb, NULL);
	bufferevent_enable(bev, EV_WRITE);
	bufferevent_enable(bev, EV_READ);
	bufferevent_settimeout(bev, precv_timeout, psend_timeout);

	zval *rsrc_result;
	MAKE_STD_ZVAL(rsrc_result);
	
	long rsrc_id = ZEND_REGISTER_RESOURCE(rsrc_result, bev, le_bufferevent);

	//printf("=======> listener_cb = [%ld]\n" , bev);
	zval *args[2];
	zval retval;
	
	MAKE_STD_ZVAL(args[0]);
	MAKE_STD_ZVAL(args[1]);
	ZVAL_RESOURCE(args[0], rsrc_id);
	ZVAL_LONG(args[1], fd);

	if (call_user_function(EG(function_table), NULL, open_callback, &retval, 2, args TSRMLS_CC) == SUCCESS) {
		pt_fl;
		zval_dtor(&retval);
	}

	zval_ptr_dtor(&(args[0]));
}

static void
conn_writecb(struct bufferevent *bev, void *user_data)
{
	//printf("=======> conn_writecb = [%ld]\n" , bev);
	
	struct evbuffer *output = bufferevent_get_output(bev);
	if (evbuffer_get_length(output) == 0) {
		//printf("flushed answer\n");
		//bufferevent_free(bev);
	}
}

static void 
conn_readcb(struct bufferevent *incoming,
                       void *arg)
{
  //printf("=======> conn_readcb = [%ld]\n" , incoming);

  struct evbuffer *input = bufferevent_get_input(incoming);
  int read_buff_len = evbuffer_get_length(input);
  
  char buffer[read_buff_len+1];
 
  int ret = bufferevent_read(incoming, &buffer, read_buff_len);
  buffer[read_buff_len] = '\0';

  if(ret)
  {
	/*
	struct evbuffer *evreturn;

	evreturn = evbuffer_new();	
	evbuffer_add_printf(evreturn,"You said %s\n",buffer);	
	bufferevent_write_buffer(incoming, evreturn);

	evbuffer_free(evreturn);
	*/

	if(strcmp(buffer, policeRequestStr)==0)//flash security message
	{
		struct evbuffer *evreturn;

		evreturn = evbuffer_new();
		evbuffer_add(evreturn, policyXML, policyXMLLen);
		bufferevent_write_buffer(incoming, evreturn);
		evbuffer_free(evreturn);
		return;
	}

	if(strcmp(buffer, HEATBEAT_MSG)==0)//heaerbeat message
	{
		struct evbuffer *evreturn;

		evreturn = evbuffer_new();
		evbuffer_add(evreturn, HEATBEAT_MSG, 2);
		bufferevent_write_buffer(incoming, evreturn);
		evbuffer_free(evreturn);
		return;
	}
	
	zval *args[2];
	zval retval;
	
	MAKE_STD_ZVAL(args[0]);
	MAKE_STD_ZVAL(args[1]);
	ZVAL_LONG(args[0], bufferevent_getfd(incoming));  
	ZVAL_STRING(args[1], buffer,1);	

	if (call_user_function(EG(function_table), NULL, read_callback, &retval, 2, args TSRMLS_CC) == SUCCESS) {
		zval_dtor(&retval);
	}

	zval_ptr_dtor(&(args[0]));
	zval_ptr_dtor(&(args[1]));
	
  }
  //printf("conn_readcb 2,ret = %d, buffer=%s \n", ret, buffer);
}


static void
conn_eventcb(struct bufferevent *bev, short events, void *user_data)
{
	//printf("=======> conn_eventcb = [%ld]\n" , bev);

	if (events & BEV_EVENT_EOF) {
		printf("Connection closed.\n");
	} else if (events & BEV_EVENT_ERROR) {
		printf("Got an error on the connection: %s\n",
		    strerror(errno));/*XXX win32*/
	}else if (events & BEV_EVENT_EOF) {
		printf("Timeout.\n");
	}
	
	zval *args[1];
	zval retval;
	
	MAKE_STD_ZVAL(args[0]);
	ZVAL_LONG(args[0], bufferevent_getfd(bev));

	if (call_user_function(EG(function_table), NULL, close_callback, &retval, 1, args TSRMLS_CC) == SUCCESS) {
		zval_dtor(&retval);
	}

	zval_ptr_dtor(&(args[0]));
	if(bev!=NULL) bufferevent_free(bev);
}

static void custom_thread(void *args)
{
	custom_fun_arg *arg=(custom_fun_arg *) args;

	zval *func = arg->runable_fun;	
	int *idx = arg->status;
 
	*idx = -1;

	zval retval;
	zval *argvs[1];	
	MAKE_STD_ZVAL(argvs[0]);
	ZVAL_LONG(argvs[0], ++custom_thread_count);

	if ( call_user_function(EG(function_table), NULL, func, &retval, 1, argvs TSRMLS_CC) == SUCCESS )
	{
		zval_dtor(&retval);
	}

	zval_ptr_dtor(&(argvs[0]));
}

static void
signal_cb(evutil_socket_t sig, short events, void *user_data)
{
	struct event_base *base = user_data;
	struct timeval delay = { 2, 0 };

	printf("Caught an interrupt signal; exiting cleanly in two seconds.\n");

	event_base_loopexit(base, &delay);
}

/**************************************************************************/
/***************************        Fast CGI Client       **************************/
/**************************************************************************/
char *makeHeader(int type, int requestId, int contentLength, int paddingLength)
{
	char *chr=calloc(1,9);
	int i1[2],i2[2];

	if(type>255||requestId>65791||contentLength>65791||paddingLength>255)
	{
		return NULL;
	}
	
	if(requestId>256)
	{
		i1[0]=ceil(requestId/256);
		i1[1]=requestId%256;
	}else{
		i1[0]=0;
		i1[1]=requestId;
	}

	if(contentLength>256)
	{
		i2[0]=ceil(contentLength/256);
		i2[1]=contentLength%256;
	}else{
		i2[0]=0;
		i2[1]=contentLength;
	}
	sprintf(chr,"%c%c%c%c%c%c%c%c",FCGI_VERSION_1,type,i1[0],i1[1],i2[0],i2[1],paddingLength,0);
	return chr;
}

char *getNameValue(char *name, char *value, int *l)
{
	char *body = makeNameValueHeader(name, value);
	char *header = makeHeader(4, 1, strlen(body), 0);
	
	char *rtn=calloc(1,strlen(body)+9);
	memcpy(rtn,header,8);
	strcpy(rtn+8,body);
	*l=8+strlen(body);
	free(body);
	free(header);
	return rtn;
}

char *makeNameValueHeader(char *name, char *value)
{
    int nameLen = strlen(name);
    int valueLen = strlen(value);

    int rtnLen=strlen(name)+strlen(value)+9;
    char *bin1=calloc(1,5);
    char *bin2=calloc(1,5);
    char *bin=calloc(1,rtnLen);

    int a=0,b=0,c=0,d=0;
    if(nameLen < 0x80)
    {
            a = nameLen;
    }else{
            a=(nameLen >> 24) & 0x7f | 0x80;
            b=(nameLen >> 16) & 0xff;
            c=(nameLen >> 8) & 0xff;
            d=nameLen & 0xff;
    }
    sprintf(bin1,"%c%c%c%c",a,b,c,d);

    a=0;b=0;c=0;d=0;
    if(valueLen < 0x80)
    {
            a= valueLen;
    }else{
            a= (valueLen >> 24) & 0x7f | 0x80;
            b= (valueLen >> 16) & 0xff;
            c= (valueLen >> 8) & 0xff;
            d= valueLen & 0xff;
    }
    sprintf(bin2,"%c%c%c%c",a,b,c,d);
    sprintf(bin,"%s%s%s%s",bin1,bin2,name,value);
    free(bin1);
    free(bin2);
    return bin;
}

/* The previous line is meant for vim and emacs, so it can correctly fold and 
   unfold functions in source code. See the corresponding marks just before 
   function definition, where the functions purpose is also documented. Please 
   follow this convention for the convenience of others editing your code.
*/


/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
