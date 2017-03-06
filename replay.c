#include "replay.h"

#define __B_Zhang__

//void main(int argc, char *argv[])
//{
//	replay(argv[1]);
//}
int main()
{
    replay("config/config.ini");
}

void replay(char *configName)
{
	struct config_info *config;
	struct trace_info *trace;
	struct req_info *req;
	int fd;
	char *buf;
	int i;
	long long initTime,nowTime,reqTime,waitTime;
    long long execTime;//for bzhang's experiment
	
	config=(struct config_info *)malloc(sizeof(struct config_info));
	memset(config,0,sizeof(struct config_info));
	trace=(struct trace_info *)malloc(sizeof(struct trace_info));
	memset(trace,0,sizeof(struct trace_info));
	req=(struct req_info *)malloc(sizeof(struct req_info));
	memset(req,0,sizeof(struct req_info));

	config_read(config,configName);
	printf("starting warm up with config %s----\n",configName);
	trace_read(config,trace);
	printf("starting replay IO trace %s----\n",config->traceFileName);

	//queue_print(trace);
	//printf("trace->inNum=%d\n",trace->inNum);
	//printf("trace->outNum=%d\n",trace->outNum);
	//printf("trace->latencySum=%lld\n",trace->latencySum);

	printf("config->device=%s\n",config->device);
	
	fd = open(config->device, O_DIRECT | O_SYNC | O_RDWR); 
	if(fd < 0) 
	{
		fprintf(stderr, "Value of errno: %d\n", errno);
       	printf("Cannot open\n");
    	exit(-1);
	}

	if (posix_memalign((void**)&buf, MEM_ALIGN, LARGEST_REQUEST_SIZE * BYTE_PER_BLOCK))
	{
		fprintf(stderr, "Error allocating buffer\n");
		return;
	}
	for(i=0;i<LARGEST_REQUEST_SIZE*BYTE_PER_BLOCK;i++)
	{
		//Generate random alphabets to write to file
		buf[i]=(char)(rand()%26+65);
	}

	init_aio();

	initTime=time_now();
    execTime=0;
	//printf("initTime=%lld\n",initTime);
	while(trace->front)
	{
		queue_pop(trace,req);
		reqTime=req->time;
		nowTime=time_elapsed(initTime);
#ifdef  __B_Zhang__
        if(nowTime-execTime > config->exec*1000000)
        {
            sleep(config->idle);
		    execTime=time_elapsed(initTime);
        }
	nowTime=time_elapsed(initTime);
#endif

		while(nowTime < reqTime)
		{
			//usleep(waitTime);
			nowTime=time_elapsed(initTime);
		}
		req->waitTime=nowTime-reqTime;
        //printf("wait time =%lld us\n",waitTime);
        
	    submit_aio(fd,buf,req,trace,initTime);
	}
    i=0;
	while(trace->inNum > trace->outNum)
	{
        i++;
        if(i>100)
        {
            break;
        }
		printf("trace->inNum=%d\n",trace->inNum);
		printf("trace->outNum=%d\n",trace->outNum);
		printf("begin sleepping 1 second------\n");
		sleep(1);
	}
	//printf("average latency= %Lf\n",(long double)trace->latencySum/(long double)trace->inNum);
	free(buf);
	free(config);
	fclose(trace->logFile);
	free(trace);
	free(req);
}

static void handle_aio(sigval_t sigval)
{
	struct aiocb_info *cb;
	int latency_submit,latency_issue;
	int error;
	int count;

	cb=(struct aiocb_info *)sigval.sival_ptr;
	latency_submit=time_elapsed(cb->beginTime_submit);
	latency_issue=time_elapsed(cb->beginTime_issue);
	//cb->trace->latencySum+=latency;

	error=aio_error(cb->aiocb);
	if(error)
	{
		if(error != ECANCELED)
		{
			fprintf(stderr,"Error completing i/o:%d\n",error);
		}
		else
		{
			printf("---ECANCELED error\n");
		}
		return;
	}
	count=aio_return(cb->aiocb);
	if(count<(int)cb->aiocb->aio_nbytes)
	{
		fprintf(stderr, "Warning I/O completed:%db but requested:%ldb\n",
			count,cb->aiocb->aio_nbytes);
	}
	fprintf(cb->trace->logFile,"%-16lf %-12lld %-12lld %-5d %-2d %-2d %d \n",
				cb->req->time,cb->req->waitTime,cb->req->lba,cb->req->size,cb->req->type,latency_submit,latency_issue);
	fflush(cb->trace->logFile);

	cb->trace->outNum++;
	//printf("cb->trace->outNum=%d\n",cb->trace->outNum);
	if(cb->trace->outNum%10000==0)
	{
		printf("---has replayed %d\n",cb->trace->outNum);
	}

	free(cb->aiocb);
	free(cb);
}

static void submit_aio(int fd, void *buf, struct req_info *req,struct trace_info *trace,long long initTime)
{
	struct aiocb_info *cb;
	char *buf_new;
	int error=0;
	//struct sigaction *sig_act;

	cb=(struct aiocb_info *)malloc(sizeof(struct aiocb_info));
	memset(cb,0,sizeof(struct aiocb_info));//where to free this?
	cb->aiocb=(struct aiocb *)malloc(sizeof(struct aiocb));
	memset(cb->aiocb,0,sizeof(struct aiocb));//where to free this?
	cb->req=(struct req_info *)malloc(sizeof(struct req_info));
	memset(cb->req,0,sizeof(struct req_info));

	cb->aiocb->aio_fildes = fd;
	cb->aiocb->aio_nbytes = req->size;
	cb->aiocb->aio_offset = req->lba;

	cb->aiocb->aio_sigevent.sigev_notify = SIGEV_THREAD;
	cb->aiocb->aio_sigevent.sigev_notify_function = handle_aio;
	cb->aiocb->aio_sigevent.sigev_notify_attributes = NULL;
	cb->aiocb->aio_sigevent.sigev_value.sival_ptr = cb;

	//error=sigaction(SIGIO,sig_act,NULL);
	//write and read different buffer
	if(USE_GLOBAL_BUFF!=1)
	{
		if (posix_memalign((void**)&buf_new, MEM_ALIGN, req->size)) 
		{
			fprintf(stderr, "Error allocating buffer\n");
		}
		cb->aiocb->aio_buf = buf_new;
	}
	else
	{
		cb->aiocb->aio_buf = buf;
	}

	//cb->req=req;	//WTF
	cb->req->time=req->time;
	cb->req->lba=req->lba;
	cb->req->size=req->size;
	cb->req->type=req->type;
    cb->req->waitTime=req->waitTime;

	/********************************/
    cb->beginTime_submit=time_now();// latency from the req was submitted
    cb->beginTime_issue=req->time+initTime; //latency from the req was issued 
    /********************************/
	cb->trace=trace;

	if(req->type==1)
	{
		error=aio_write(cb->aiocb);
	}
	else //if(req->type==0)
	{
		error=aio_read(cb->aiocb);
	}
	//while(aio_error(cb->aiocb)==EINPROGRESS);
	if(error)
	{
		fprintf(stderr, "Error performing i/o");
		exit(-1);
	}
}

static void init_aio()
{
	struct aioinit aioParam={0};
	//memset(aioParam,0,sizeof(struct aioinit));
	//two thread for each device is better
	aioParam.aio_threads = AIO_THREAD_POOL_SIZE;
	aioParam.aio_num = 2048;
	aioParam.aio_idle_time = 1;	
	aio_init(&aioParam);
}

void config_read(struct config_info *config,const char *filename)
{
	int name,value;
	char line[BUFSIZE];
	char *ptr;
	FILE *configFile;
	
	configFile=fopen(filename,"r");
	if(configFile==NULL)
	{
		printf("error: opening config file\n");
		exit(-1);
	}
	//read config file
	memset(line,0,sizeof(char)*BUFSIZE);
	while(fgets(line,sizeof(line),configFile))
	{
		if(line[0]=='#'||line[0]==' ') 
		{
			continue;
		}
    	ptr=strchr(line,'=');
	    if(!ptr)
		{
			continue;
		} 
       	name=ptr-line;	//the end of name string+1
       	value=name+1;	//the start of value string
	    while(line[name-1]==' ') 
		{
			name--;
		}
       	line[name]=0;

		if(strcmp(line,"device")==0)
		{
			sscanf(line+value,"%s",config->device);
		}
		else if(strcmp(line,"trace")==0)
		{
			sscanf(line+value,"%s",config->traceFileName);
		}
		else if(strcmp(line,"log")==0)
		{
			sscanf(line+value,"%s",config->logFileName);
		}
		else if(strcmp(line,"exectime")==0)
		{
			sscanf(line+value,"%d",&config->exec);
		}
		else if(strcmp(line,"idletime")==0)
		{
			sscanf(line+value,"%d",&config->idle);
		}
		memset(line,0,sizeof(char)*BUFSIZE);
	}
	fclose(configFile);
}

void trace_read(struct config_info *config,struct trace_info *trace)
{
	FILE *traceFile;
	char line[BUFSIZE];
	struct req_info* req;

	traceFile=fopen(config->traceFileName,"r");
	req=(struct req_info *)malloc(sizeof(struct req_info));
	if(traceFile==NULL)
	{
		printf("error: opening trace file\n");
		exit(-1);
	}
	//initialize trace file parameters
	trace->inNum=0;
	trace->outNum=0;
	trace->latencySum=0;
	trace->logFile=fopen(config->logFileName,"w");

	while(fgets(line,sizeof(line),traceFile))
	{
		if(strlen(line)==2)
		{
			continue;
		}
		trace->inNum++;	//track the process of IO requests
        //time:ms, lba:sectors, size:sectors, type:1<->write 0<-->read
		sscanf(line,"%lf %lld %d %d",&req->time,&req->lba,&req->size,&req->type);
		//push into request queue
		req->time=req->time*1000;	//ms-->us
		req->size=req->size*BYTE_PER_BLOCK;
		req->lba=req->lba*BYTE_PER_BLOCK;
        req->waitTime=0;
		queue_push(trace,req);
	}
	fclose(traceFile);
}

long long time_now()
{
	struct timeval now;
	gettimeofday(&now,NULL);
	return 1000000*now.tv_sec+now.tv_usec;	//us
}

long long time_elapsed(long long begin)
{
	return time_now()-begin;	//us
}

void queue_push(struct trace_info *trace,struct req_info *req)
{
	struct req_info* temp;
	temp = (struct req_info *)malloc(sizeof(struct req_info));
	temp->time = req->time;
	temp->lba = req->lba;
	temp->size = req->size;
	temp->type = req->type;
    temp->waitTime=req->waitTime;
	temp->next = NULL;
	if(trace->front == NULL && trace->rear == NULL)
	{
		trace->front = trace->rear = temp;
	}
	else
	{
		trace->rear->next = temp;
		trace->rear = temp;
	}
}

void queue_pop(struct trace_info *trace,struct req_info *req) 
{
	struct req_info* temp = trace->front;
	if(trace->front == NULL) 
	{
		printf("Queue is Empty\n");
		return;
	}
	req->time = trace->front->time;
	req->lba  = trace->front->lba;
	req->size = trace->front->size;
	req->type = trace->front->type;	
    req->waitTime=trace->front->waitTime;
	if(trace->front == trace->rear) 
	{
		trace->front = trace->rear = NULL;
	}
	else 
	{
		trace->front = trace->front->next;
	}
	free(temp);
}

void queue_print(struct trace_info *trace)
{
	struct req_info* temp = trace->front;
	while(temp) 
	{
		printf("%lf ",temp->time);
		printf("%lld ",temp->lba);
		printf("%d ",temp->size);
		printf("%d\n",temp->type);
		printf("%lld\n",temp->waitTime);
		temp = temp->next;
	}
}
