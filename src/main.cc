/*
	Flexisip, a flexible SIP proxy server with media capabilities.
    Copyright (C) 2010  Belledonne Communications SARL.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License as
    published by the Free Software Foundation, either version 3 of the
    License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <syslog.h>
#include <sys/time.h>
#include <sys/resource.h>


#include "transcodeagent.hh"
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdlib.h>

#define IPADDR_SIZE 32

static int get_local_ip_for_with_connect(int type, const char *dest, char *result){
	int err,tmp;
	struct addrinfo hints;
	struct addrinfo *res=NULL;
	struct sockaddr_storage addr;
	int sock;
	socklen_t s;

	memset(&hints,0,sizeof(hints));
	hints.ai_family=(type==AF_INET6) ? PF_INET6 : PF_INET;
	hints.ai_socktype=SOCK_DGRAM;
	/*hints.ai_flags=AI_NUMERICHOST|AI_CANONNAME;*/
	err=getaddrinfo(dest,"5060",&hints,&res);
	if (err!=0){
		LOGE("getaddrinfo() error: %s",gai_strerror(err));
		return -1;
	}
	if (res==NULL){
		LOGE("bug: getaddrinfo returned nothing.");
		return -1;
	}
	sock=socket(res->ai_family,SOCK_DGRAM,0);
	tmp=1;
	err=setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,&tmp,sizeof(int));
	if (err<0){
		LOGW("Error in setsockopt: %s",strerror(errno));
	}
	err=connect(sock,res->ai_addr,res->ai_addrlen);
	if (err<0) {
		LOGE("Error in connect: %s",strerror(errno));
 		freeaddrinfo(res);
 		close(sock);
		return -1;
	}
	freeaddrinfo(res);
	res=NULL;
	s=sizeof(addr);
	err=getsockname(sock,(struct sockaddr*)&addr,&s);
	if (err!=0) {
		LOGE("Error in getsockname: %s",strerror(errno));
		close(sock);
		return -1;
	}
	
	err=getnameinfo((struct sockaddr *)&addr,s,result,IPADDR_SIZE,NULL,0,NI_NUMERICHOST);
	if (err!=0){
		LOGE("getnameinfo error: %s",strerror(errno));
	}
	close(sock);
	LOGI("Local interface to reach %s is %s.",dest,result);
	return 0;
}

static void usage(const char *arg0){
	printf("%s\n"
	       "\t\t [--port <port number to listen>]\n"
	       "\t\t [--debug]\n"
	       "\t\t [--daemon]\n"
	       "\t\t [--help]\n",arg0);
	exit(-1);
}

static void syslogHandler(OrtpLogLevel log_level, const char *str, va_list l){
	int syslev=LOG_ALERT;
	switch(log_level){
		case ORTP_DEBUG:
			syslev=LOG_DEBUG;
			break;
		case ORTP_MESSAGE:
			syslev=LOG_INFO;
			break;
/*			
		case ORTP_NOTICE:
			syslev=LOG_NOTICE;
			break;
*/
		 case ORTP_WARNING:
			syslev=LOG_WARNING;
			break;
		case ORTP_ERROR:
			syslev=LOG_ERR;
		case ORTP_FATAL:
			syslev=LOG_ALERT;
			break;
		default:
			syslev=LOG_ERR;
	}
	vsyslog(syslev,str,l);
	va_end(l);
}

int main(int argc, char *argv[]){
	Agent *a;
	int port=5060;
	char localip[IPADDR_SIZE];
	int i;
	const char *pidfile=NULL;
	bool debug=false;
	bool daemon=false;
	bool useSyslog=false;

	for(i=1;i<argc;++i){
		if (strcmp(argv[i],"--port")==0){
			i++;
			if (i<argc){
				port=atoi(argv[i]);
				continue;
			}
		}else if (strcmp(argv[i],"--pidfile")==0){
			i++;
			if (i<argc){
				pidfile=argv[i];
				continue;
			}
		}else if (strcmp(argv[i],"--daemon")==0){
			daemon=true;
			continue;
		}else if (strcmp(argv[i],"--syslog")==0){
			useSyslog=true;
			continue;
		}else if (strcmp(argv[i],"--debug")==0){
			debug=true;
			continue;
		}
		fprintf(stderr,"Bad option %s\n",argv[i]);
		usage(argv[0]);
	}

	if (daemon){
		pid_t pid = fork();
		int fd;
		
		if (pid < 0){
			fprintf(stderr,"Could not fork\n");
			exit(-1);
		}
		if (pid > 0) {
			exit(0);
		}
		/*here we are the new process*/
		setsid();
		
		fd = open("/dev/null", O_RDWR);
		if (fd==-1){
			fprintf(stderr,"Could not open /dev/null\n");
			exit(-1);
		}
		dup2(fd, 0);
		dup2(fd, 1);
		dup2(fd, 2);
		close(fd);
	}

	if (pidfile){
		FILE *f=fopen(pidfile,"w");
		fprintf(f,"%i",getpid());
		fclose(f);
	}
	
	if (useSyslog){
		openlog("flexisip", 0, LOG_USER);
		setlogmask(~0);
		ortp_set_log_handler(syslogHandler);
	}
	ortp_set_log_level_mask(ORTP_DEBUG|ORTP_MESSAGE|ORTP_WARNING|ORTP_ERROR);
	LOGN("Starting version %s", VERSION);

	
	
	ortp_init();
	ortp_set_log_file(stdout);
	if (debug==false){
		ortp_set_log_level_mask(ORTP_WARNING|ORTP_ERROR);
	}
	ms_init();

	/*enable core dumps*/
	struct rlimit lm;
	lm.rlim_cur=RLIM_INFINITY;
	lm.rlim_max=RLIM_INFINITY;
	if (setrlimit(RLIMIT_CORE,&lm)==-1){
		LOGE("Cannot enable core dump, setrlimit() failed: %s",strerror(errno));
	}
	
	su_init();
	su_root_t *root=su_root_create(NULL);

	get_local_ip_for_with_connect (AF_INET,"87.98.157.38",localip);
	
	a=new TranscodeAgent(root,localip,port);
	a->loadConfig (ConfigManager::get());
	do{
		//su_root_run(root);
		su_root_sleep(root,5000);
		a->idle();
	}while(1);
	delete a;
    su_root_destroy(root);
	
}

