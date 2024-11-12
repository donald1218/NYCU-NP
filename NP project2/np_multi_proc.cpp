#include <iomanip>
#include <iostream>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <fcntl.h>
#include <queue>
#include <map>
#include <utility> 
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <pthread.h>
using namespace std;

#define MAX_USER 30
#define SHMKEY ((key_t) 1438)
#define PERM 0666
// sem_t mutex;

void sig_handler(int sig);
void broadcast(string msg,int targetid);
void who(int id);
void yell(int id,string msg);
void name(int id,char* newname);
void tell(int id,int target_id,string msg);
bool checkuserin(int inputid,string totalcmd);
bool checkuserout(int outputid,string totalcmd);
int parse_command(char input[15000],vector<int> &nonwaitpid,int fd,int id);
int execute_cmd(vector<char*> tmp,int* pipein,int* pipeout,int type);
int redirect(char* cmd,char* filename,int* pipein,int fd);
void clean_queue(int numpipe,vector<int> &nonwaitpid,int inputid);
void user_pipe(int inputid,int outputid,vector<int> &nonwaitpid,int fd);
int parse_number(char* cmd,int num);
int type_push(char* cmd);
int configure_id();
void DeleteUser(int target_id);
void removefifo(int inputid,int outputid);
map<int,int*> numberpiped;
queue<vector<char*>> command;
queue<int> type;//0:non 1:| 2:! 3:>
vector<int> nonwaitpid;
int ins=0;
int id=0;
int writefifofd[31];
int readfifofd[31];
bool has_numpipe=false;

struct user{
    char ipaddr[31][16];
    bool usedid[31];
    char nickname[31][21];
    int  c_pid[31];
    char mesg[31][1025];
    bool havemesg[31][31];
    bool sendfifo[31][31];
    bool isopen[31][31];
};
struct user *share;


void perr(string str,int err_no){
    cout<<str<<endl<<"errno "<<strerror(err_no)<<endl;
    exit(-1);
}

int handle_request(int fd,int id){
    cout<<"% ";
    char input[15000];
    cin.getline(input,15000);
    if(parse_command(input,nonwaitpid,fd,id)==-1){
        return -1;
    }
    return 0;
}

int passiveTCP(char* service,int qlen){
    struct servent *pse;
    struct protoent *ppe;
    struct sockaddr_in sin;
    int s;
    int iopt = 1;
    bzero((char*)&sin,sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    if(pse = getservbyname(service,"tcp")){
        sin.sin_port = htons(ntohs((u_short)pse->s_port));
    }else if((sin.sin_port = htons((u_short)atoi(service)))==0){
        perr("get service error",errno);
    }
    if((ppe = getprotobyname("tcp"))==0){
        perr("get protocol error",errno);
    }
    s = socket(PF_INET,SOCK_STREAM,ppe->p_proto);
    if(s<0){
        perr("creat socket error",errno);
    }
    if(setsockopt(s,SOL_SOCKET,SO_REUSEADDR,&iopt,sizeof(iopt))<0){
        perr("setsockopt error",errno);
    }
    if(bind(s,(struct sockaddr*)&sin,sizeof(sin))<0){
        perr("bind error",errno);
    }
    if(listen(s,qlen)<0){
        perr("listen error",errno);
    }
    return s;
}

void doTCP(int msock);

void reaper(int sig){
    int	status;
    for(int i=0;i<31;i++){
        waitpid(-1,nullptr,WNOHANG);
    }
}

void shminit(){
    int shmid = 0;
    if((shmid = shmget(SHMKEY, sizeof(user), PERM | IPC_CREAT)) < 0){
        cerr << "server err: shmget failed (errno #" << errno << ")" << endl;
        exit(1);
    }
    if((share = (user*) shmat(shmid, NULL, 0)) == (user*) -1){
        cerr << "server err: shmat faild" << endl;
    }
    memset((char*) share, 0, sizeof(user));
    for(int i=0;i<31;i++){
        share->usedid[i]=false;
        for(int j=0;j<31;j++){
            share->havemesg[i][j]=false;
        }
    }
}


int main(int argc,char* argv[]){
    char *service;
    struct sockaddr_in fsin;
    int msock;
    // sem_init(&mutex,0,1);
    socklen_t alen;
    if(argc==2){
        service = argv[1];
    }else if(argc>2){
        exit(-1);
    }
    shminit();
    msock = passiveTCP(service,MAX_USER);
    (void) signal(SIGCHLD, reaper);
    signal(SIGINT,sig_handler);
    signal(SIGQUIT,sig_handler);
    signal(SIGTERM,sig_handler);
    while(true){
        doTCP(msock);
    }
    // sem_destroy(&mutex);
    return EXIT_SUCCESS;
}

void doTCP(int msock){
    struct sockaddr_in fsin;
    unsigned int	alen;
    int	fd, ssock;
	alen = sizeof(fsin);
    ssock = accept(msock, (struct sockaddr *)&fsin, &alen);
	if (ssock < 0){
        if(errno == EINTR){
            cerr<<"accept error"<<endl<<errno<<endl;
            return;
        }
		perr("accept fail", errno);
    }
    int pid=fork();
    while(pid==-1){
        waitpid(-1,nullptr,0);
        pid = fork();
    }
    switch(pid){
        case 0:
            close(msock);
            
            break;
        default:
            close(ssock);
            return;
    }
    for(int i=0;i<31;i++){
        readfifofd[i]=0;
        writefifofd[i]=0;
    }
    dup2(ssock,STDIN_FILENO);
    dup2(ssock,STDOUT_FILENO);
    dup2(ssock,STDERR_FILENO);
    signal(SIGUSR1, sig_handler);
    signal(SIGUSR2, sig_handler);
    string str;
    str += inet_ntoa(fsin.sin_addr);
    str += ":";
    str += to_string(htons(fsin.sin_port));
    id = configure_id();
    strncpy(share->ipaddr[id], str.c_str(),sizeof(share->ipaddr[id]));
    strncpy(share->nickname[id],"(no name)",sizeof(share->nickname[id]));
    share->c_pid[id]=getpid();
    str = "*** User '(no name)' entered from " + str;
    str += ". ***";
    broadcast("****************************************",id);
    broadcast("** Welcome to the information server. **",id);
    broadcast("****************************************",id);
    broadcast(str,0);
    setenv("PATH","bin:.",1);
    while(handle_request(ssock,id)!=-1);
    DeleteUser(id);
    close(ssock);
    while(wait(nullptr)>0);
	exit(0);
}



int execute_cmd(vector<char*> tmp,int* pipein,int* pipeout,int type){
    char* argv[tmp.size()+1];
    for (int i=0;i<tmp.size();i++){
        argv[i]=tmp[i];
    }
    argv[tmp.size()]=NULL;
    int pid = fork();
    while(pid==-1){
        waitpid(-1,nullptr,0);
        pid = fork();
    }
    if(pid==0){
        if(pipein!=NULL){
            close(pipein[1]);
            dup2(pipein[0],STDIN_FILENO);
        }
        if(pipeout!=NULL){
            close(pipeout[0]);
            if(type==2){
                close(STDERR_FILENO);
                dup(pipeout[1]);
            }
            dup2(pipeout[1],STDOUT_FILENO);
        }
        if(execvp(argv[0],argv)<0){
            cerr<<"Unknown command: ["<<argv[0]<<"]."<<endl<<flush;
            exit(EXIT_FAILURE);
        }
    }else{
        if(pipein!=NULL){
            close(pipein[1]);
            close(pipein[0]);
        }
    }
    return pid;
}

int redirect(char* cmd,char* filename,int* pipein,int fd){
    int pid = fork();
    while(pid==-1){
        waitpid(-1,nullptr,0);
        pid = fork();
    }
    if(pid==0){
        if(pipein!=NULL){
            close(pipein[1]);
            dup2(pipein[0],STDIN_FILENO);
        }
        dup2(fd,STDOUT_FILENO);
        if(execlp(cmd,cmd,filename,NULL)<0){
            cerr<<"Unknown command: ["<<cmd<<"]."<<flush<<endl<<flush;
            exit(EXIT_FAILURE);
        }
    }else{
        if(pipein!=NULL){
            close(pipein[1]);
            close(pipein[0]);
        }
        close(fd);
    }
    return pid;   
}

void clean_queue(int numpipe,vector<int> &nonwaitpid,int inputid){
    queue<int> pids;
    int pid_c;
    bool first_cmd = true;
    int pipecnt = 0;
    int pipes[1000][2];
    while(!command.empty()){
        if(first_cmd){
            if(inputid==0){
                auto iter = numberpiped.find(ins+numpipe);
                auto iter2 = numberpiped.find(ins);
                if(command.size()==1){
                    if(iter == numberpiped.end()){
                        int* npipe = new int[2];
                        pipe(npipe);
                        numberpiped[ins+numpipe]=npipe;
                        if(iter2 == numberpiped.end()){
                            pid_c = execute_cmd(command.front(),NULL,npipe,type.front());
                        }else{
                            pid_c = execute_cmd(command.front(),iter2->second,npipe,type.front());
                            numberpiped.erase(iter2);
                        }
                        pids.push(pid_c);
                    }else{
                        if(iter2 == numberpiped.end()){
                            pid_c = execute_cmd(command.front(),NULL,iter->second,type.front());
                        }else{
                            pid_c = execute_cmd(command.front(),iter2->second,iter->second,type.front());
                            numberpiped.erase(iter2);
                        }
                        pids.push(pid_c);
                    }
                    command.pop();
                    type.pop();
                    int n=0;
                    while(!pids.empty()){
                        n = 0;
                        while(waitpid(pids.front(),nullptr,WNOHANG)==0){
                            n++;
                            usleep(1000*100);
                            if(n>3){
                                nonwaitpid.push_back(pids.front());
                                break;
                            }
                        }
                        pids.pop();
                    }
                    for(int i=0;i<nonwaitpid.size();i++){
                        if(waitpid(nonwaitpid[i],nullptr,WNOHANG)!=0){
                            nonwaitpid.erase(nonwaitpid.begin()+i);
                        }
                    }
                    continue;
                }else{
                    pipe(pipes[pipecnt%1000]);     
                    if(iter2==numberpiped.end()){
                        pid_c = execute_cmd(command.front(),NULL,pipes[pipecnt%1000],type.front());
                    }else{
                        pid_c = execute_cmd(command.front(),iter2->second,pipes[pipecnt%1000],type.front());
                    }
                }
                pids.push(pid_c);
                first_cmd = false;
            }else{
                int *inputfifo = new int[2];
                inputfifo[0] = readfifofd[inputid];
                inputfifo[1] = open("/dev/null",O_RDWR);
                if(command.size()==1){
                    auto iter = numberpiped.find(ins+numpipe);
                    if(iter == numberpiped.end()){
                        int* npipe = new int[2];
                        pipe(npipe);
                        numberpiped[ins+numpipe]=npipe;
                        pid_c = execute_cmd(command.front(),inputfifo,npipe,type.front());
                        pids.push(pid_c);
                    }else{
                        pid_c = execute_cmd(command.front(),inputfifo,iter->second,type.front());
                        pids.push(pid_c);
                    }
                    close(inputfifo[1]);
                    removefifo(inputid,id);
                    command.pop();
                    type.pop();
                    int n=0;
                    while(!pids.empty()){
                        n = 0;
                        while(waitpid(pids.front(),nullptr,WNOHANG)==0){
                            n++;
                            usleep(1000*100);
                            if(n>3){
                                nonwaitpid.push_back(pids.front());
                                break;
                            }
                        }
                        pids.pop();
                    }
                    for(int i=0;i<nonwaitpid.size();i++){
                        if(waitpid(nonwaitpid[i],nullptr,WNOHANG)!=0){
                            nonwaitpid.erase(nonwaitpid.begin()+i);
                        }
                    }
                    continue;
                }
                pipe(pipes[pipecnt%1000]);     
                pid_c = execute_cmd(command.front(),NULL,pipes[pipecnt%1000],type.front());
                pids.push(pid_c);
                first_cmd = false;
            }
        }else{
            if(command.size()==1){
                auto iter = numberpiped.find(ins+numpipe);
                if(iter == numberpiped.end()){
                    int *npipe = new int[2];
                    pipe(npipe);
                    numberpiped[ins+numpipe]=npipe;
                    pid_c = execute_cmd(command.front(),pipes[(pipecnt-1)%1000],npipe,type.front());
                }else{
                    pid_c = execute_cmd(command.front(),pipes[(pipecnt-1)%1000],iter->second,type.front());
                }
                pids.push(pid_c);
            }else{
                pipe(pipes[pipecnt%1000]);
                pid_c = execute_cmd(command.front(),pipes[(pipecnt-1)%1000],pipes[pipecnt%1000],type.front());
                pids.push(pid_c);
            }
        }
        pipecnt++;
        command.pop();
        type.pop();
        for(int qs = 0;qs<pids.size();qs++){
            if(waitpid(pids.front(),nullptr,WNOHANG)!=0){
                pids.pop();
            }
        }
    }
    int n=0;
    while(!pids.empty()){
        n = 0;
        while(waitpid(pids.front(),nullptr,WNOHANG)==0){
            n++;
            usleep(1000*100);
            if(n>3){
                nonwaitpid.push_back(pids.front());
                break;
            }
        }
        pids.pop();
    }
    for(int i=0;i<nonwaitpid.size();i++){
        if(waitpid(nonwaitpid[i],nullptr,WNOHANG)!=0){
            nonwaitpid.erase(nonwaitpid.begin()+i);
        }
    }
}


int parse_command(char input[15000],vector<int> &nonwaitpid,int fd,int id){
    bool first_cmd = true;
    string totalcmd = input;
    if(totalcmd[totalcmd.length()-1]=='\n'||totalcmd[totalcmd.length()-1]=='\r'){
        totalcmd.erase(totalcmd.length()-1);
    }
    if(totalcmd.length()==0){
        return 0;
    }
    char* cmd;
    char* path;
    queue<int> pids;
    bool erroruser = false;
    int pipes[1000][2];
    int pipecnt = 0;
    int pid_c;
    const char* delimiters = " \r\n";
    int inputid = 0;
    int FD_NULL[2];
    FD_NULL[0] = open("/dev/null",O_RDWR);
    FD_NULL[1] = open("/dev/null",O_RDWR);
    cmd = strtok(input,delimiters);
    if(strcmp(cmd,"exit")==0){
        return -1;
    }else if(strcmp(cmd,"printenv")==0){
        char* target = strtok(NULL,delimiters);
        if(target != NULL && getenv(target)!=NULL){
            dup2(fd,STDOUT_FILENO);
            cout<<getenv(target)<<endl;
            dup2(STDOUT_FILENO,fd);
        }
        return 0;
    }else if(strcmp(cmd,"setenv")==0){
        char* target = strtok(NULL,delimiters);
        char* envvar = strtok(NULL,delimiters);
        setenv(target,envvar,1);
        return 0;
    }else if(strcmp(cmd,"who")==0){
        who(id);
        return 0;
    }else if(strcmp(cmd,"yell")==0){
        char* msg = strtok(NULL,delimiters);
        string str = totalcmd.substr(5);
        while(msg != NULL){
            msg = strtok(NULL,delimiters);
        }
        yell(id,str);
        return 0;
    }else if(strcmp(cmd,"name")==0){
        char* newname = strtok(NULL,delimiters);
        name(id,newname);
        return 0;
    }else if(strcmp(cmd,"tell")==0){
        int target_id = atoi(strtok(NULL,delimiters));
        char* msg = strtok(NULL,delimiters);
        int sub=5;
        if(target_id>=10){
            sub += 3;
        }else{
            sub += 2;
        }
        string str = totalcmd.substr(sub);
        while(msg!=NULL){
            msg = strtok(NULL,delimiters);
        }
        tell(id,target_id,str);
        return 0;
    }
    while(cmd!=NULL){
        vector<char*> tmp;
        char* cmds = cmd;
        cmd = strtok(NULL,delimiters);
        tmp.clear();
        tmp.push_back(cmds);
        if(cmd==NULL){
            command.push(tmp);
            type.push(0);
        }else{
            if(cmd[0]=='!'||cmd[0]=='|'||cmd[0]=='>'){
                if(strlen(cmd)>1){
                    int numpipe;
                    if(cmd[0]=='>'){
                        numpipe = parse_number(cmd,10000);
                    }else{
                        numpipe = parse_number(cmd,1000);
                    }
                    command.push(tmp);
                    if(type_push(cmd)<3){
                        clean_queue(numpipe,nonwaitpid,inputid);
                    }else{
                        if(inputid==0){
                            cmd = strtok(NULL,delimiters);
                            if(cmd != NULL){
                                inputid = parse_number(cmd,10000);
                                erroruser = checkuserin(inputid,totalcmd);
                            }
                        }
                        erroruser = (erroruser|checkuserout(numpipe,totalcmd));
                        if(erroruser){
                            inputid=0;
                            numpipe=0;
                            erroruser = false;
                        }
                        user_pipe(inputid,numpipe,nonwaitpid,fd);
                    }
                    ins++;
                    has_numpipe=true;
                    first_cmd = true;

                }else{
                    command.push(tmp);
                    type_push(cmd);
                }
            }else{
                path = cmd;
                if(cmd[0]!='<'){
                    tmp.push_back(path);
                }else{
                    inputid = parse_number(cmd,10000);
                    erroruser = checkuserin(inputid,totalcmd);                
                }
                while(cmd[0]!='!'&&cmd[0]!='|'&&cmd[0] != '>'){
                    cmd = strtok(NULL,delimiters);
                    if(cmd==NULL){
                        break;
                    }
                    if(cmd[0]!='!'&&cmd[0]!='|'&&cmd[0] != '>'){
                        if(cmd[0]!='<'){
                            tmp.push_back(cmd);
                        }else{
                            inputid = parse_number(cmd,10000);
                            erroruser = checkuserin(inputid,totalcmd); 
                        }
                    }
                }
                if(cmd!=NULL){
                    if(strlen(cmd)>1){
                        int numpipe;
                        if(cmd[0]=='>'){
                            numpipe = parse_number(cmd,10000);
                        }
                        else{
                            numpipe = parse_number(cmd,1000);
                        }
                        command.push(tmp);
                        if(type_push(cmd)<3){
                            clean_queue(numpipe,nonwaitpid,inputid);
                        }else{
                            if(inputid==0){
                                cmd = strtok(NULL,delimiters);
                                if(cmd != NULL){
                                    inputid = parse_number(cmd,10000);
                                    erroruser = checkuserin(inputid,totalcmd);                                    
                                }                        
                            }
                            erroruser = (erroruser|checkuserout(numpipe,totalcmd));
                            if(erroruser){
                                inputid = 0;
                                numpipe = 0;
                                erroruser = false;
                            }
                            user_pipe(inputid,numpipe,nonwaitpid,fd);                         
                        }
                        has_numpipe = true;
                        ins++;
                        first_cmd = true;
                    }else{
                        command.push(tmp);
                        type_push(cmd);
                    }
                }else{
                    command.push(tmp);
                    type.push(0);
                }
            }
        }
        cmd = strtok(NULL,delimiters);
    }
    if(erroruser){
        inputid=0;
    }
    while(!command.empty()){
        if(first_cmd){
            if(inputid==0){
                auto iter = numberpiped.find(ins);
                if(command.size()==1){
                    if(iter!=numberpiped.end()){
                        pid_c = execute_cmd(command.front(),iter->second,NULL,type.front());
                        pids.push(pid_c);
                        numberpiped.erase(iter);
                    }else{
                        if(erroruser){
                            pid_c = execute_cmd(command.front(),FD_NULL,NULL,type.front());
                        }else{
                            pid_c = execute_cmd(command.front(),NULL,NULL,type.front());
                        }
                        pids.push(pid_c);
                    }
                    command.pop();
                    type.pop();
                    while(!pids.empty()){
                        waitpid(pids.front(),nullptr,0);
                        pids.pop();
                    }
                    continue;
                }else{
                    if(command.size()==2&&type.front()==3){
                        auto rcmd = command.front();
                        command.pop();
                        type.pop();
                        auto fn = command.front()[0];
                        int rfd = open(fn,O_CREAT|O_TRUNC|O_RDWR,S_IRWXU);
                        if(iter!=numberpiped.end()){
                            if(rcmd.size()>1){
                                pid_c = redirect(rcmd[0],rcmd[1],iter->second,rfd);
                            }else{
                                pid_c = redirect(rcmd[0],NULL,iter->second,rfd);
                            }
                            pids.push(pid_c);
                        }else{
                            if(rcmd.size()>1){
                                pid_c = redirect(rcmd[0],rcmd[1],NULL,rfd);
                            }else{
                                pid_c = redirect(rcmd[0],NULL,NULL,rfd);
                            }
                            pids.push(pid_c);
                        }
                    }else{
                        pipe(pipes[pipecnt%1000]);
                        if(iter!=numberpiped.end()){
                            pid_c = execute_cmd(command.front(),iter->second,pipes[pipecnt%1000],type.front());
                            numberpiped.erase(iter);
                            pids.push(pid_c);
                        }else{
                            if(erroruser){
                                pid_c = execute_cmd(command.front(),FD_NULL,pipes[pipecnt%1000],type.front());
                            }else{
                                pid_c = execute_cmd(command.front(),NULL,pipes[pipecnt%1000],type.front());
                            }
                            pids.push(pid_c);
                        }
                        first_cmd = false;
                    }
                }
            }else{
                int *inputfifo = new int[2];
                inputfifo[0] = readfifofd[inputid];
                inputfifo[1] = open("/dev/null",O_RDWR);
                if(command.size()==1){
                    pid_c = execute_cmd(command.front(),inputfifo,NULL,type.front());
                    pids.push(pid_c);
                    removefifo(inputid,id);
                    command.pop();
                    type.pop();
                    while(!pids.empty()){
                        waitpid(pids.front(),nullptr,0);
                        pids.pop();
                    }
                    continue;
                }else{
                    if(command.size()==2&&type.front()==3){
                        auto rcmd = command.front();
                        command.pop();
                        type.pop();
                        auto fn = command.front()[0];
                        int rfd = open(fn,O_CREAT|O_TRUNC|O_RDWR,S_IRWXU);
                        if(rcmd.size()>1){
                            pid_c = redirect(rcmd[0],rcmd[1],inputfifo,rfd);
                        }else{
                            pid_c = redirect(rcmd[0],NULL,inputfifo,rfd);
                        }
                    }else{
                        pipe(pipes[pipecnt%1000]);
                        pid_c = execute_cmd(command.front(),inputfifo,pipes[pipecnt%1000],type.front());
                    }
                    pids.push(pid_c);
                    first_cmd = false;
                    removefifo(inputid,id);
                }                
            }
        }else{
            if(command.size()==1){
                pid_c = execute_cmd(command.front(),pipes[(pipecnt-1)%1000],NULL,type.front());
                pids.push(pid_c);
            }else{
                if(command.size()==2&&type.front()==3){
                    auto rcmd = command.front();
                    command.pop();
                    type.pop();
                    auto fn = command.front()[0];
                    int rfd = open(fn,O_CREAT|O_TRUNC|O_RDWR,S_IRWXU);
                    if(rcmd.size()>1){
                        pid_c = redirect(rcmd[0],rcmd[1],pipes[(pipecnt-1)%1000],rfd);
                    }else{
                        pid_c = redirect(rcmd[0],NULL,pipes[(pipecnt-1)%1000],rfd);
                    }
                    pids.push(pid_c);
                }else{
                    pipe(pipes[pipecnt%1000]);
                    pid_c = execute_cmd(command.front(),pipes[(pipecnt-1)%1000],pipes[pipecnt%1000],type.front());
                    pids.push(pid_c);
                }
            }
        }
        pipecnt++;
        command.pop();
        type.pop();
        for(int qs = 0;qs<pids.size();qs++){
            if(waitpid(pids.front(),nullptr,WNOHANG)!=0){
                pids.pop();
            }
        }
    }
    while(!pids.empty()){
        waitpid(pids.front(),nullptr,0);
        pids.pop();
    }
    close(FD_NULL[0]);
    close(FD_NULL[1]);
    if(!has_numpipe){
        ins++;
    }else{
        has_numpipe = false;
    }
    return 0;
}

int type_push(char* cmd){
    if(cmd[0]=='|'){
        type.push(1);
        return 1;
    }else if(cmd[0]=='!'){
        type.push(2);
        return 2;
    }else if(strlen(cmd)==1&&cmd[0]=='>'){
        type.push(3);
        return 3;
    }else if(strlen(cmd)>1&&cmd[0]=='>'){
        type.push(4);
        return 4;
    }
    return 0;
}

int parse_number(char* cmd,int num){
    int numpipe=0;
    for(int i=1;i<strlen(cmd);i++){
        numpipe*=10;
        numpipe+=(int)(cmd[i]-'0');
    }
    if(numpipe>num){
        numpipe=num;
    }
    if(numpipe<1){
        numpipe=1;
    }
    return numpipe;    
}

int configure_id(){
    for(int i=1;i<31;i++){
        if(share->usedid[i]==false){
            share->usedid[i]=true;
            return i;
        }
    }
    return 0;
}

void sig_handler(int sig){
    if(sig==SIGUSR1){
        for(int i=0;i<31;i++){
            if(share->havemesg[i][id]){
                cout<<share->mesg[i]<<endl;
                share->havemesg[i][id]=false;
            }
        }
        // cout<<share->mesg[id]<<endl;
        // share->havemesg[id]=false;
    }else if(sig==SIGUSR2){
        for (int i=0;i<31;i++){
            if(share->sendfifo[i][id]){
                if(!share->isopen[i][id]){
                    string str="./user_pipe/fifo " + to_string(i) + "_"+to_string(id);
                    readfifofd[i] = open(str.c_str(),O_RDONLY | O_NONBLOCK);
                    share->isopen[i][id] = true;
                }
            }
        }
    }else if(sig==SIGINT||sig==SIGTERM||sig==SIGQUIT){
        if(id!=0){
            DeleteUser(id);
            while(wait(nullptr)>0);
            exit(0);
        }else{
            for(int i=1;i<31;i++){
                if(share->usedid[i]){
                    kill(share->c_pid[i],SIGINT);
                }
            }
            while(wait(nullptr)>0);
            int shmid = shmget(SHMKEY,sizeof (user), PERM);
            shmdt(share);
            if(shmctl(shmid,IPC_RMID, NULL)<0){
                cerr << "server err: shmctl IPC_RMID failed"<<errno << endl;
            }
            // sem_destroy(&mutex);
            exit(0);
        }
    }
}

void broadcast(string msg,int targetid){
    // sem_wait(&mutex); 
    if(targetid==0){
        for(int i=1;i<31;i++){
            while(share->havemesg[id][i]);  
        }
        for(int i=1;i<31;i++){
            if(share->usedid[i]){
                share->havemesg[id][i]=true;
                strncpy(share->mesg[id],msg.c_str(),1025);
                kill(share->c_pid[i],SIGUSR1);
            }
        }
    }else{
        while(share->havemesg[id][targetid]);
        share->havemesg[id][targetid]=true;
        strncpy(share->mesg[id],msg.c_str(),1025);
        kill(share->c_pid[targetid],SIGUSR1);
    }
    usleep(100*20);
    // sem_post(&mutex);
}

void DeleteUser(int target_id){
    string str = "*** User '"+string(share->nickname[id])+"' left. ***";
    broadcast(str,0);
    share->usedid[target_id]=false;
    strncpy(share->nickname[target_id],"",sizeof(share->nickname[target_id]));
    strncpy(share->ipaddr[target_id],"",sizeof(share->ipaddr[target_id]));
    share->c_pid[target_id] = 0;
    for(int i=0;i<31;i++){
        while(share->havemesg[id][i]);
        share->havemesg[i][id]=false;
    }
    strncpy(share->mesg[target_id],"",sizeof(share->mesg[target_id]));
    for(int i=1;i<31;i++){
        if(share->isopen[target_id][i]){
            string str="./user_pipe/fifo " + to_string(target_id) + "_"+to_string(i);
            close(writefifofd[i]);
            unlink(str.c_str());
            share->isopen[target_id][i]=false;
            share->sendfifo[target_id][i]=false;
        }
        if(share->isopen[i][target_id]){
            string str="./user_pipe/fifo " + to_string(i) + "_"+to_string(target_id);
            close(readfifofd[i]);
            unlink(str.c_str());
            share->isopen[i][target_id]=false;
            share->sendfifo[i][target_id]=false;            
        }
    }
    shmdt(share);
}

void who(int id){
    cout<<"<ID> <nickname>  <IP:port>   <indicate me>"<<endl;
    for(int i=1;i<31;i++){
        if(share->usedid[i]){
            cout<<i<<"  "<<share->nickname[i]<<"   "<<share->ipaddr[i]<<"    ";
            if(i==id){
                cout<<"<-me";
            }
            cout<<endl;
        }
    }
}

void yell(int id,string msg){
    string str = "*** " + string(share->nickname[id]) + " yelled ***: "+msg;
    broadcast(str,0);
}

void name(int id,char* newname){
    bool isexist = false;
    for(int i=0;i<31;i++){
        if(strcmp(share->nickname[i],newname)==0){
            isexist = true;
            break;
        }
    }
    if(isexist){
        cout<<"*** User '"<<newname<<"' already exists. ***"<<endl;
    }else{
        strncpy(share->nickname[id],newname,sizeof(share->nickname[id]));
        string str = "*** User from "+string(share->ipaddr[id])+"  is named '"+newname+"'. ***";
        broadcast(str,0);
    }
}

void tell(int id,int target_id,string msg){
    if(target_id>30||target_id<1||share->usedid[target_id]==false){
        cout<<"*** Error: user #"<<target_id<<" does not exist yet. ***"<<endl;
    }else{
        string str;
        str = "*** " + string(share->nickname[id]) +"  told you ***: "+msg;
        broadcast(str,target_id);
    }
}

void user_pipe(int inputid,int outputid,vector<int> &nonwaitpid,int fd){
    queue<int> pids;
    int pid_c;
    bool first_cmd = true;
    int pipecnt = 0;
    int pipes[1000][2];
    while(!command.empty()){
        if(first_cmd){
            if(inputid==0){
                auto iter = numberpiped.find(ins);
                if(command.size()==1){
                    int* npipe = new int[2];
                    if(outputid!=0){
                        string str="./user_pipe/fifo "+ to_string(id) + "_"+to_string(outputid);
                        if(mkfifo(str.c_str(),0600)<0){
                            cerr << "error: failed to create FIFO" << errno << endl;
                        }else{
                            share->sendfifo[id][outputid] = true;
                            kill(share->c_pid[outputid],SIGUSR2);
                            writefifofd[outputid] = open(str.c_str(),O_WRONLY);
                            npipe[0] = open("/dev/null",O_RDWR);
                            npipe[1] = writefifofd[outputid];
                        }
                    }else{
                        npipe[0] = open("/dev/null",O_RDWR);
                        npipe[1] = open("/dev/null",O_RDWR);
                    }
                    if(iter == numberpiped.end()){
                        pid_c = execute_cmd(command.front(),NULL,npipe,type.front());
                    }else{
                        pid_c = execute_cmd(command.front(),iter->second,npipe,type.front());
                        numberpiped.erase(iter);
                    }
                    pids.push(pid_c);
                    command.pop();
                    type.pop();
                    close(npipe[0]);
                    close(npipe[1]);
                    while(!pids.empty()){
                        int n = 0;
                        while(waitpid(pids.front(),nullptr,WNOHANG)==0){
                            n++;
                            usleep(1000*100);
                            if(n>3){
                                nonwaitpid.push_back(pids.front());
                                break;
                            }
                        }
                        pids.pop();
                    }
                    for(int i=0;i<nonwaitpid.size();i++){
                        if(waitpid(nonwaitpid[i],nullptr,WNOHANG)!=0){
                            nonwaitpid.erase(nonwaitpid.begin()+i);
                        }
                    }
                    continue;
                }else{
                    pipe(pipes[pipecnt%1000]);
                    if(iter == numberpiped.end()){
                        pid_c = execute_cmd(command.front(),NULL,pipes[pipecnt%1000],type.front());
                    }else{
                        pid_c = execute_cmd(command.front(),iter->second,pipes[pipecnt%1000],type.front());
                        numberpiped.erase(iter);
                    }
                    pids.push(pid_c);
                    first_cmd = false;
                }
            }else{ 
                int *inputfifo = new int[2];
                inputfifo[0] = readfifofd[inputid];
                inputfifo[1] = open("/dev/null",O_RDWR);
                if(command.size()==1){
                    int* npipe = new int[2];
                    if(outputid!=0){
                        string str="./user_pipe/fifo "+ to_string(id) + "_"+to_string(outputid);
                        if(mkfifo(str.c_str(),0600)<0){
                            cerr << "error: failed to create FIFO" << errno << endl;
                        }else{
                            share->sendfifo[id][outputid] = true;
                            kill(share->c_pid[outputid],SIGUSR2);
                            writefifofd[outputid] = open(str.c_str(),O_WRONLY);
                            npipe[0] = open("/dev/null",O_RDWR);
                            npipe[1] = writefifofd[outputid];
                        }
                    }else{
                        npipe[0] = open("/dev/null",O_RDWR);
                        npipe[1] = open("/dev/null",O_RDWR);
                    }
                    pid_c = execute_cmd(command.front(),inputfifo,npipe,type.front());
                    close(inputfifo[1]);
                    removefifo(inputid,id);
                    pids.push(pid_c);
                    command.pop();
                    type.pop();
                    close(npipe[0]);
                    close(npipe[1]);
                    while(!pids.empty()){
                        int n = 0;
                        while(waitpid(pids.front(),nullptr,WNOHANG)==0){
                            n++;
                            usleep(1000*100);
                            if(n>3){
                                nonwaitpid.push_back(pids.front());
                                break;
                            }
                        }
                        pids.pop();
                    }
                    for(int i=0;i<nonwaitpid.size();i++){
                        if(waitpid(nonwaitpid[i],nullptr,WNOHANG)!=0){
                            nonwaitpid.erase(nonwaitpid.begin()+i);
                        }
                    }
                    continue;
                }else{
                    pipe(pipes[pipecnt%1000]);  
                    pid_c = execute_cmd(command.front(),inputfifo,pipes[pipecnt%1000],type.front());
                    close(inputfifo[1]);
                    removefifo(inputid,id);
                    pids.push(pid_c);
                    first_cmd = false;
                }
            }
        }else{
            if(command.size()==1){
                int *npipe = new int[2];
                if(outputid!=0){
                    string str="./user_pipe/fifo "+ to_string(id) + "_"+to_string(outputid);
                    if(mkfifo(str.c_str(),0600)<0){
                        cerr << "error: failed to create FIFO" << errno << endl;
                    }else{
                        share->sendfifo[id][outputid] = true;
                        kill(share->c_pid[outputid],SIGUSR2);
                        writefifofd[outputid] = open(str.c_str(),O_WRONLY);
                        npipe[0] = open("/dev/null",O_RDWR);
                        npipe[1] = writefifofd[outputid];
                    }
                }else{
                    npipe[0] = open("/dev/null",O_RDWR);
                    npipe[1] = open("/dev/null",O_RDWR);
                }
                pid_c = execute_cmd(command.front(),pipes[(pipecnt-1)%1000],npipe,type.front());
                close(npipe[0]);
                close(npipe[1]);
                pids.push(pid_c);
            }else{
                pipe(pipes[pipecnt%1000]);
                pid_c = execute_cmd(command.front(),pipes[(pipecnt-1)%1000],pipes[pipecnt%1000],type.front());
                pids.push(pid_c);
            }
        }
        pipecnt++;
        command.pop();
        type.pop();
        for(int qs = 0;qs<pids.size();qs++){
            if(waitpid(pids.front(),nullptr,WNOHANG)!=0){
                pids.pop();
            }
        }
    }
    int n=0;
    while(!pids.empty()){
        n = 0;
        while(waitpid(pids.front(),nullptr,WNOHANG)==0){
            n++;
            usleep(1000*100);
            if(n>3){
                nonwaitpid.push_back(pids.front());
                break;
            }
        }
        pids.pop();
    }
    for(int i=0;i<nonwaitpid.size();i++){
        if(waitpid(nonwaitpid[i],nullptr,WNOHANG)!=0){
            nonwaitpid.erase(nonwaitpid.begin()+i);
        }
    }
}

bool checkuserin(int inputid,string totalcmd){
    bool erroruser = false;
    if(inputid<0||inputid>30){
        cout<<"*** Error: user #"+to_string(inputid)+" does not exist yet. ***"<<endl;;
        erroruser = true;
    }else{
        if(share->usedid[inputid]==false){
            cout<<"*** Error: user #"+to_string(inputid)+" does not exist yet. ***"<<endl;
            erroruser = true;
        }else{
            if(share->isopen[inputid][id]){
                string bstr = "*** "+string(share->nickname[id])+" (#"+to_string(id)+") just received from "+string(share->nickname[inputid])+" (#" +to_string(inputid)+") by '";
                bstr+=totalcmd+"' ***";
                broadcast(bstr,0);
            }else{
                string bstr = "*** Error: the pipe #"+to_string(inputid)+"->#"+to_string(id)+" does not exist yet. ***";
                erroruser = true;
                cout<<bstr<<endl;
            }                     
        }
    }
    return erroruser;
}

bool checkuserout(int outputid,string totalcmd){
    bool erroruser = false;
    if(share->usedid[outputid]==false){
        cout<<"*** Error: user #"+to_string(outputid)+" does not exist yet. ***"<<endl;
        erroruser = true;
    }else{
        if(!share->isopen[id][outputid]){
            string pipestr = "*** "+string(share->nickname[id])+" (#"+to_string(id)+") just piped '";
            pipestr += totalcmd;
            pipestr += "' to "+string(share->nickname[outputid])+" (#" +to_string(outputid)+") ***";
            broadcast(pipestr,0);
        }else{
            cout<<"*** Error: the pipe #"+to_string(id)+"->#"+to_string(outputid)+" already exists. ***"<<endl;
            erroruser = true;
        }
    }
    return erroruser;
}

void removefifo(int inputid,int outputid){
    close(readfifofd[inputid]);
    string fifoname;
    fifoname = "./user_pipe/fifo " + to_string(inputid) + "_"+to_string(id);
    unlink(fifoname.c_str());
    share->sendfifo[inputid][outputid]=false;
    share->isopen[inputid][outputid]=false;
}