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
int child_num = 0;
using namespace std;

void broadcast(string msg);
void who(int fd);
void yell(int fd,string msg);
void name(int fd,char* newname);
void tell(int fd,int target_id,string msg);
void errmsg(int fd,string str);
bool checkuserin(int fd,int inputid,string totalcmd);
bool checkuserout(int fd,int outputid,string totalcmd);
int parse_command(char input[15000],vector<int> &nonwaitpid,bool &has_numpipe,int fd);
int execute_cmd(vector<char*> tmp,int* pipein,int* pipeout,int type,int fd);
int redirect(char* cmd,char* filename,int* pipein,int fd,int sockfd);
void clean_queue(int numpipe,vector<int> &nonwaitpid,int fd,int inputid);
void user_pipe(int inputid,int outputid,vector<int> &nonwaitpid,int fd);
int parse_number(char* cmd,int num);
int type_push(char* cmd);
bool configure_id(int fd);
void DeleteUser(int fd);
map<int,map<int,int*>> numberpiped;
queue<vector<char*>> command;
queue<int> type;//0:non 1:| 2:! 3:>
vector<int> nonwaitpid;
map<int,map<string,string>> env;
map<int,int> fd2id;
map<int,string> nickname;
map<int,string> IP_port;
map<int,map<int,int*>> userpipe;
int ins[31];
bool usedid[31];
int saveinfd = dup(STDIN_FILENO);
int saveoutfd = dup(STDOUT_FILENO);
extern char **environ;

void perr(string str,int err_no){
    cout<<str<<endl<<"errno "<<strerror(err_no)<<endl;
    exit(-1);
}

int handle_request(int fd,bool &has_numpipe){
    char input[15000];
    dup2(fd,STDIN_FILENO);
    cin.getline(input,15000);
    if(parse_command(input,nonwaitpid,has_numpipe,fd)==-1){
        dup2(saveinfd,STDIN_FILENO);
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

int main(int argc,char* argv[]){
    char *service;
    struct sockaddr_in fsin;
    int msock;
    map<char*,char*> initenv;
    for(int i=0;environ[i]!=NULL;i++){
        char *name,*envpath;
        name = strtok(environ[i],"=");
        envpath = strtok(NULL,"");
        if(envpath!=NULL){
            initenv[name] = envpath;
        }
    }
    fd_set rfds;
    fd_set afds;
    socklen_t alen;
    
    int fd,nfds;
    if(argc==2){
        service = argv[1];
    }else if(argc>2){
        exit(-1);
    }
    msock = passiveTCP(service,30);
    FD_ZERO(&afds);
    FD_ZERO(&rfds);
    FD_SET(msock,&afds);
    nfds = 1024;
    bool has_numpipe[31];
    
    for(int i=0;i<31;i++){
        has_numpipe[i]=false;
        usedid[i]=false;
    }

    while(true){
        bcopy(&afds,&rfds,sizeof(rfds));
        if(select(nfds+1,&rfds,NULL,NULL,NULL)<0){
            if(errno == EINTR){
                continue;
            }else{
                perr("select error",errno);
            }
        }
        if(FD_ISSET(msock,&rfds)){
            int ssock;
            alen = sizeof(fsin);
            ssock = accept(msock,(struct sockaddr *)&fsin,&alen);
            if(ssock<0){
                perr("accept failed",errno);
            }
	    FD_SET(ssock,&afds);
            configure_id(ssock);
            int id = fd2id[ssock];
            env[id]["PATH"]="bin:.";
            ins[id] = 0;
            string str;
            str += inet_ntoa(fsin.sin_addr);
            str += ":";
            str += to_string(htons(fsin.sin_port));
            IP_port[id] = str;
            str = "*** User '(no name)' entered from " + str;
            str += ". ***";
            nickname[id] = "(no name)";
            dup2(ssock,STDOUT_FILENO);
            cout<<"****************************************"<<endl;
            cout<<"** Welcome to the information server. **"<<endl;
            cout<<"****************************************"<<endl;
            dup2(saveoutfd,STDOUT_FILENO);
            broadcast(str);
            dup2(ssock,STDOUT_FILENO);
            cout<<"% "<<flush;
            dup2(saveoutfd,STDOUT_FILENO);
        }
        for(fd = 0;fd<1024;fd++){
            if(fd!=msock&&FD_ISSET(fd,&rfds)){
                clearenv();
                for(auto iter = initenv.begin();iter != initenv.end();iter++){
                    setenv(iter->first,iter->second,1);
                }
                int id = fd2id[fd];
                for (auto iter = env[id].begin();iter!=env[id].end();iter++){
                    setenv(iter->first.c_str(),iter->second.c_str(),1);
                }
                if(handle_request(fd,has_numpipe[id])==-1){
                    DeleteUser(fd);
                    FD_CLR(fd,&afds);
                }else{
                    dup2(fd,STDOUT_FILENO);
                    cout<<"% "<<flush;
                    dup2(saveoutfd,STDOUT_FILENO);
                }
            }
        }
    }
    return EXIT_SUCCESS;
}


int execute_cmd(vector<char*> tmp,int* pipein,int* pipeout,int type,int fd){
    char* argv[tmp.size()+1];
    for (int i=0;i<tmp.size();i++){
        argv[i]=tmp[i];
    }
    argv[tmp.size()]=NULL;
    int pid = fork();
    while(pid==-1){
        waitpid(-1,nullptr,0);
        child_num--;
        pid = fork();
    }
    if(pid==0){
        if(pipein!=NULL){
            close(pipein[1]);
            dup2(pipein[0],STDIN_FILENO);
        }else{
            dup2(fd,STDIN_FILENO);
        }
        if(pipeout!=NULL){
            close(pipeout[0]);
            if(type==2){
                close(STDERR_FILENO);
                dup(pipeout[1]);
            }
            dup2(pipeout[1],STDOUT_FILENO);
            dup2(fd,STDERR_FILENO);
        }else{
            dup2(fd,STDOUT_FILENO);
            dup2(fd,STDERR_FILENO);
        }
        if(execvp(argv[0],argv)<0){
            dup2(saveoutfd,STDOUT_FILENO);
            cerr<<"Unknown command: ["<<argv[0]<<"]."<<endl<<flush;
            exit(EXIT_FAILURE);
        }
    }else{
        child_num++;
        if(pipein!=NULL){
            close(pipein[1]);
            close(pipein[0]);
        }
    }
    return pid;
}

int redirect(char* cmd,char* filename,int* pipein,int fd,int sockfd){
    int pid = fork();
    while(pid==-1){
        waitpid(-1,nullptr,0);
        child_num--;
        pid = fork();
    }
    if(pid==0){
        if(pipein!=NULL){
            close(pipein[1]);
            dup2(pipein[0],STDIN_FILENO);
        }
        dup2(fd,STDOUT_FILENO);
        if(execlp(cmd,cmd,filename,NULL)<0){
            dup2(saveoutfd,STDOUT_FILENO);
            dup2(sockfd,STDERR_FILENO);
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

void clean_queue(int numpipe,vector<int> &nonwaitpid,int fd,int inputid){
    queue<int> pids;
    int pid_c;
    bool first_cmd = true;
    int pipecnt = 0;
    int pipes[1000][2];
    int id = fd2id[fd];
    while(!command.empty()){
        if(first_cmd){
            if(inputid==0){
                auto iter = numberpiped[id].find(ins[id]+numpipe);
                auto iter2 = numberpiped[id].find(ins[id]);
                if(command.size()==1){
                    if(iter == numberpiped[id].end()){
                        int* npipe = new int[2];
                        pipe(npipe);
                        numberpiped[id][ins[id]+numpipe]=npipe;
                        if(iter2 == numberpiped[id].end()){
                            pid_c = execute_cmd(command.front(),NULL,npipe,type.front(),fd);
                        }else{
                            pid_c = execute_cmd(command.front(),iter2->second,npipe,type.front(),fd);
                            numberpiped[id].erase(iter2);
                        }
                        pids.push(pid_c);
                    }else{
                        if(iter2 == numberpiped[id].end()){
                            pid_c = execute_cmd(command.front(),NULL,iter->second,type.front(),fd);
                        }else{
                            pid_c = execute_cmd(command.front(),iter2->second,iter->second,type.front(),fd);
                            numberpiped[id].erase(iter2);
                        }
                        pids.push(pid_c);
                    }
                    command.pop();
                    type.pop();
                    continue;
                }else{
                    pipe(pipes[pipecnt%1000]);     
                    if(iter2 == numberpiped[id].end()){
                        pid_c = execute_cmd(command.front(),NULL,pipes[pipecnt%1000],type.front(),fd);
                    }else{
                        pid_c = execute_cmd(command.front(),iter2->second,pipes[pipecnt%1000],type.front(),fd);
                        numberpiped[id].erase(iter2);
                    }
                }
                pids.push(pid_c);
                first_cmd = false;
            }else{
                if(command.size()==1){
                    auto iter = numberpiped[id].find(ins[id]+numpipe);
                    if(iter == numberpiped[id].end()){
                        int* npipe = new int[2];
                        pipe(npipe);
                        numberpiped[id][ins[id]+numpipe]=npipe;
                        pid_c = execute_cmd(command.front(),userpipe[inputid][id],npipe,type.front(),fd);
                    }else{
                        pid_c = execute_cmd(command.front(),userpipe[inputid][id],iter->second,type.front(),fd);
                    }
                    userpipe[inputid].erase(id);
                    pids.push(pid_c);
                    command.pop();
                    type.pop();
                    continue;
                }
                pipe(pipes[pipecnt%1000]);     
                pid_c = execute_cmd(command.front(),NULL,pipes[pipecnt%1000],type.front(),fd);
                pids.push(pid_c);
                first_cmd = false;
            }
        }else{
            if(command.size()==1){
                auto iter = numberpiped[id].find(ins[id]+numpipe);
                if(iter == numberpiped[id].end()){
                    int *npipe = new int[2];
                    pipe(npipe);
                    numberpiped[id][ins[id]+numpipe]=npipe;
                    pid_c = execute_cmd(command.front(),pipes[(pipecnt-1)%1000],npipe,type.front(),fd);
                }else{
                    pid_c = execute_cmd(command.front(),pipes[(pipecnt-1)%1000],iter->second,type.front(),fd);
                }
                pids.push(pid_c);
            }else{
                pipe(pipes[pipecnt%1000]);
                pid_c = execute_cmd(command.front(),pipes[(pipecnt-1)%1000],pipes[pipecnt%1000],type.front(),fd);
                pids.push(pid_c);
            }
        }
        pipecnt++;
        command.pop();
        type.pop();
        while(child_num>50){
            if(waitpid(pids.front(),nullptr,WNOHANG)!=0){
                child_num--;
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
                child_num++;
                break;
            }
        }
        child_num--;
        pids.pop();
    }
    for(int i=0;i<nonwaitpid.size();i++){
        if(waitpid(nonwaitpid[i],nullptr,WNOHANG)!=0){
            nonwaitpid.erase(nonwaitpid.begin()+i);
            child_num--;
        }
    }
}


int parse_command(char input[15000],vector<int> &nonwaitpid,bool &has_numpipe,int fd){
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
    if(cmd!=NULL){
        if(!has_numpipe){
            ins[fd2id[fd]]++;
        }else{
            has_numpipe = false;
        }
    }
    int id = fd2id[fd];
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
        string t = target;
        string e = envvar;
        env[id][t] = e;
        setenv(target,envvar,1);
        return 0;
    }else if(strcmp(cmd,"who")==0){
        who(fd);
        return 0;
    }else if(strcmp(cmd,"yell")==0){
        char* msg = strtok(NULL,delimiters);
        string str = totalcmd.substr(5);
        while(msg != NULL){
            msg = strtok(NULL,delimiters);
        }
        yell(fd,str);
        return 0;
    }else if(strcmp(cmd,"name")==0){
        char* newname = strtok(NULL,delimiters);
        name(fd,newname);
        return 0;
    }else if(strcmp(cmd,"tell")==0){
        int target_id = atoi(strtok(NULL,delimiters));
        char* msg = strtok(NULL,delimiters);
        int sub = 5;
        if(target_id>=10){
            sub += 3;
        }else{
            sub += 2;
        }
        string str = totalcmd.substr(sub);
        while(msg!=NULL){
            msg = strtok(NULL,delimiters);
        }
        tell(fd,target_id,str);
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
                        clean_queue(numpipe,nonwaitpid,fd,inputid);
                    }else{
                        if(inputid==0){
                            cmd = strtok(NULL,delimiters);
                            if(cmd != NULL){
                                inputid = parse_number(cmd,10000);
                                erroruser = checkuserin(fd,inputid,totalcmd);
                            }
                        }
                        erroruser = (erroruser|checkuserout(fd,numpipe,totalcmd));
                        if(erroruser){
                            inputid=0;
                            numpipe=0;
                            erroruser = false;
                        }
                        user_pipe(inputid,numpipe,nonwaitpid,fd);
                    }
                    first_cmd = true;
                    ins[id]++;
                    has_numpipe=true;
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
                    erroruser = checkuserin(fd,inputid,totalcmd);                
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
                            erroruser = checkuserin(fd,inputid,totalcmd); 
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
                            clean_queue(numpipe,nonwaitpid,fd,inputid);
                        }else{
                            if(inputid==0){
                                cmd = strtok(NULL,delimiters);
                                if(cmd != NULL){
                                    inputid = parse_number(cmd,10000);
                                    erroruser = checkuserin(fd,inputid,totalcmd);                                    
                                }                        
                            }
                            erroruser = (erroruser|checkuserout(fd,numpipe,totalcmd));
                            if(erroruser){
                                inputid = 0;
                                numpipe = 0;
                                erroruser = false;
                            }
                            user_pipe(inputid,numpipe,nonwaitpid,fd);                         
                        }
                        first_cmd = true;
                        has_numpipe = true;
                        ins[id]++;
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
                auto iter = numberpiped[id].find(ins[id]);
                if(command.size()==1){
                    if(iter!=numberpiped[id].end()){
                        pid_c = execute_cmd(command.front(),iter->second,NULL,type.front(),fd);
                        pids.push(pid_c);
                        numberpiped[id].erase(iter);
                    }else{
                        if(erroruser){
                            pid_c = execute_cmd(command.front(),FD_NULL,NULL,type.front(),fd);
                        }else{
                            pid_c = execute_cmd(command.front(),NULL,NULL,type.front(),fd);
                            pids.push(pid_c);
                        }
                    }
                    command.pop();
                    type.pop();
                    continue;
                }else{
                    if(command.size()==2&&type.front()==3){
                        auto rcmd = command.front();
                        command.pop();
                        type.pop();
                        auto fn = command.front()[0];
                        int rfd = open(fn,O_CREAT|O_TRUNC|O_RDWR,S_IRWXU);
                        if(iter!=numberpiped[id].end()){
                            if(rcmd.size()>1){
                                pid_c = redirect(rcmd[0],rcmd[1],iter->second,rfd,fd);
                            }else{
                                pid_c = redirect(rcmd[0],NULL,iter->second,rfd,fd);
                            }
                            pids.push(pid_c);
                        }else{
                            if(rcmd.size()>1){
                                pid_c = redirect(rcmd[0],rcmd[1],NULL,rfd,fd);
                            }else{
                                pid_c = redirect(rcmd[0],NULL,NULL,rfd,fd);
                            }
                            pids.push(pid_c);
                        }
                    }else{
                        pipe(pipes[pipecnt%1000]);
                        if(iter!=numberpiped[id].end()){
                            pid_c = execute_cmd(command.front(),iter->second,pipes[pipecnt%1000],type.front(),fd);
                            numberpiped[id].erase(iter);
                            pids.push(pid_c);
                        }else{
                            if(erroruser){
                                pid_c = execute_cmd(command.front(),FD_NULL,pipes[pipecnt%1000],type.front(),fd);
                            }else{
                                pid_c = execute_cmd(command.front(),NULL,pipes[pipecnt%1000],type.front(),fd);
                            }
                            pids.push(pid_c);
                        }
                        first_cmd = false;
                    }
                }
            }else{
                if(command.size()==1){
                    pid_c = execute_cmd(command.front(),userpipe[inputid][id],NULL,type.front(),fd);
                    pids.push(pid_c);
                    userpipe[inputid].erase(id);
                    command.pop();
                    type.pop();
                    continue;
                }else{
                    if(command.size()==2&&type.front()==3){
                        auto rcmd = command.front();
                        command.pop();
                        type.pop();
                        auto fn = command.front()[0];
                        int rfd = open(fn,O_CREAT|O_TRUNC|O_RDWR,S_IRWXU);
                        if(rcmd.size()>1){
                            pid_c = redirect(rcmd[0],rcmd[1],userpipe[inputid][id],rfd,fd);
                        }else{
                            pid_c = redirect(rcmd[0],NULL,userpipe[inputid][id],rfd,fd);
                        }
                    }else{
                        pipe(pipes[pipecnt%1000]);
                        pid_c = execute_cmd(command.front(),userpipe[inputid][id],pipes[pipecnt%1000],type.front(),fd);
                    }
                    userpipe[inputid].erase(id);
                    pids.push(pid_c);
                    first_cmd = false;
                }                
            }
        }else{
            if(command.size()==1){
                pid_c = execute_cmd(command.front(),pipes[(pipecnt-1)%1000],NULL,type.front(),fd);
                pids.push(pid_c);
            }else{
                if(command.size()==2&&type.front()==3){
                    auto rcmd = command.front();
                    command.pop();
                    type.pop();
                    auto fn = command.front()[0];
                    int rfd = open(fn,O_CREAT|O_TRUNC|O_RDWR,S_IRWXU);
                    if(rcmd.size()>1){
                        pid_c = redirect(rcmd[0],rcmd[1],pipes[(pipecnt-1)%1000],rfd,fd);
                    }else{
                        pid_c = redirect(rcmd[0],NULL,pipes[(pipecnt-1)%1000],rfd,fd);
                    }
                    pids.push(pid_c);
                }else{
                    pipe(pipes[pipecnt%1000]);
                    pid_c = execute_cmd(command.front(),pipes[(pipecnt-1)%1000],pipes[pipecnt%1000],type.front(),fd);
                    pids.push(pid_c);
                }
            }
        }
        pipecnt++;
        command.pop();
        type.pop();
        while(child_num>50){
            if(waitpid(pids.front(),nullptr,WNOHANG)!=0){
                child_num--;
                pids.pop();
            }
        }
    }
    while(!pids.empty()){
        waitpid(pids.front(),nullptr,0);
        child_num--;
        pids.pop();
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

bool configure_id(int fd){
    for(int i=1;i<31;i++){
        if(usedid[i]==false){
            usedid[i]=true;
            fd2id[fd] = i;
            return true;
        }
    }
    return false;
}

void broadcast(string msg){
    for(auto &fd : fd2id){
        dup2(fd.first,STDOUT_FILENO);
        cout<<msg<<endl;
    }
    dup2(saveoutfd,STDOUT_FILENO);
}

void DeleteUser(int fd){
    int id = fd2id[fd];
    string str = "*** User '"+nickname[id]+"' left. ***";
    broadcast(str);
    usedid[id]=false;
    numberpiped.erase(id);
    env.erase(id);
    nickname.erase(id);
    IP_port.erase(id);
    for(auto iter = userpipe.begin();iter != userpipe.end();iter++){
        iter->second.erase(id);
    }
    userpipe.erase(id);
    fd2id.erase(fd);
    close(fd);
}

void who(int fd){
    dup2(fd,STDOUT_FILENO);
    cout<<"<ID> <nickname>  <IP:port>   <indicate me>"<<endl;
    for(int i=1;i<31;i++){
        if(usedid[i]){
            cout<<i<<"  "<<nickname[i]<<"   "<<IP_port[i]<<"    ";
            if(fd2id[fd]==i){
                cout<<"<-me";
            }
            cout<<endl;
        }
    }
    dup2(saveoutfd,STDOUT_FILENO);
}

void yell(int fd,string msg){
    string str = "*** " + nickname[fd2id[fd]] + " yelled ***: "+msg;
    broadcast(str);
}

void name(int fd,char* newname){
    bool isexist = false;
    for(auto iter = nickname.begin();iter!=nickname.end();iter++){
        if(iter->second==newname){
            isexist = true;
            break;
        }
    }
    if(isexist){
        dup2(fd,STDOUT_FILENO);
        cout<<"*** User '"<<newname<<"' already exists. ***"<<endl;
        dup2(saveoutfd,STDOUT_FILENO);
    }else{
        nickname[fd2id[fd]] = newname;
        string str = "*** User from "+IP_port[fd2id[fd]]+"  is named '"+newname+"'. ***";
        broadcast(str);
    }
}

void tell(int fd,int target_id,string msg){
    if(target_id>30||target_id<1||usedid[target_id]==false){
        dup2(fd,STDOUT_FILENO);
        cout<<"*** Error: user #"<<target_id<<" does not exist yet. ***"<<endl;
        dup2(saveoutfd,STDOUT_FILENO);
    }else{
        for(auto iter = fd2id.begin();iter!=fd2id.end();iter++){
            if(iter->second==target_id){
                dup2(iter->first,STDOUT_FILENO);
                cout<<"*** "<<nickname[fd2id[fd]]<<" told you ***: "<<msg<<endl;
                dup2(saveoutfd,STDOUT_FILENO);
                break;
            }
        }
    }
}

void user_pipe(int inputid,int outputid,vector<int> &nonwaitpid,int fd){
    queue<int> pids;
    int pid_c;
    bool first_cmd = true;
    int pipecnt = 0;
    int pipes[1000][2];
    int id = fd2id[fd];
    while(!command.empty()){
        if(first_cmd){
            if(inputid==0){
                auto iter = numberpiped[id].find(ins[id]);
                if(command.size()==1){
                    int* npipe = new int[2];
                    if(outputid!=0){
                        pipe(npipe);
                        userpipe[id][outputid]=npipe;
                    }else{
                        npipe[0] = open("/dev/null",O_RDWR);
                        npipe[1] = open("/dev/null",O_RDWR);
                    }
                    if(iter == numberpiped[id].end()){
                        pid_c = execute_cmd(command.front(),NULL,npipe,type.front(),fd);
                    }else{
                        pid_c = execute_cmd(command.front(),iter->second,npipe,type.front(),fd);
                        numberpiped[id].erase(iter);
                    }
                    pids.push(pid_c);
                    command.pop();
                    type.pop();
                    continue;
                }else{
                    pipe(pipes[pipecnt%1000]);
                    if(iter == numberpiped[id].end()){
                        pid_c = execute_cmd(command.front(),NULL,pipes[pipecnt%1000],type.front(),fd);
                    }else{
                        pid_c = execute_cmd(command.front(),iter->second,pipes[pipecnt%1000],type.front(),fd);
                        numberpiped[id].erase(iter);
                    }
                    pids.push(pid_c);
                    first_cmd = false;
                }
            }else{  
                if(command.size()==1){
                    int* npipe = new int[2];
                    if(outputid!=0){
                        pipe(npipe);
                        userpipe[id][outputid]=npipe;
                    }else{
                        npipe[0] = open("/dev/null",O_RDWR);
                        npipe[1] = open("/dev/null",O_RDWR);
                    }
                    pid_c = execute_cmd(command.front(),userpipe[inputid][id],npipe,type.front(),fd);
                    userpipe[inputid].erase(id);
                    pids.push(pid_c);
                    command.pop();
                    type.pop();
                    continue;
                }else{
                    pipe(pipes[pipecnt%1000]);  
                    pid_c = execute_cmd(command.front(),userpipe[inputid][id],pipes[pipecnt%1000],type.front(),fd);
                    userpipe[inputid].erase(id);
                    pids.push(pid_c);
                    first_cmd = false;
                }
            }
        }else{
            if(command.size()==1){
                int* npipe = new int[2];
                if(outputid!=0){
                    pipe(npipe);
                    userpipe[id][outputid]=npipe;
                }else{
                    npipe[0] = open("/dev/null",O_RDWR);
                    npipe[1] = open("/dev/null",O_RDWR);
                }
                pid_c = execute_cmd(command.front(),pipes[(pipecnt-1)%1000],npipe,type.front(),fd);
                pids.push(pid_c);
            }else{
                pipe(pipes[pipecnt%1000]);
                pid_c = execute_cmd(command.front(),pipes[(pipecnt-1)%1000],pipes[pipecnt%1000],type.front(),fd);
                pids.push(pid_c);
            }
        }
        pipecnt++;
        command.pop();
        type.pop();
        while(child_num>50){
            if(waitpid(pids.front(),nullptr,WNOHANG)!=0){
                child_num--;
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
                child_num++;
                break;
            }
        }
        child_num--;
        pids.pop();
    }
    for(int i=0;i<nonwaitpid.size();i++){
        if(waitpid(nonwaitpid[i],nullptr,WNOHANG)!=0){
            nonwaitpid.erase(nonwaitpid.begin()+i);
            child_num--;
        }
    }
}

void errmsg(int fd,string str){
    dup2(fd,STDOUT_FILENO);
    cout<<str<<endl;
    dup2(saveoutfd,STDOUT_FILENO);
}

bool checkuserin(int fd,int inputid,string totalcmd){
    bool erroruser = false;
    int id = fd2id[fd];
    if(inputid<0||inputid>30){
        errmsg(fd,"*** Error: user #"+to_string(inputid)+" does not exist yet. ***");
        erroruser = true;
    }else{
        if(usedid[inputid]==false){
            errmsg(fd,"*** Error: user # "+to_string(inputid)+" does not exist yet. ***");
            erroruser = true;
        }else{
            auto iter = userpipe[inputid].find(id);
            if(iter!=userpipe[inputid].end()){
                string bstr = "*** "+nickname[id]+" (#"+to_string(id)+") just received from "+nickname[inputid]+" (#" +to_string(inputid)+") by '";
                bstr+=totalcmd+"' ***";
                broadcast(bstr);
            }else{
                string bstr = "*** Error: the pipe #"+to_string(inputid)+"->#"+to_string(id)+" does not exist yet. ***";
                erroruser = true;
                errmsg(fd,bstr);
            }                     
        }
    }
    return erroruser;
}

bool checkuserout(int fd,int outputid,string totalcmd){
    bool erroruser = false;
    int id = fd2id[fd];
    if(outputid<1||outputid>30||usedid[outputid]==false){
        errmsg(fd,"*** Error: user #"+to_string(outputid)+" does not exist yet. ***");
        erroruser = true;
    }else{
        auto iter = userpipe[id].find(outputid);
        if(iter==userpipe[id].end()){
            string pipestr = "*** "+nickname[id]+" (#"+to_string(id)+") just piped '";
            pipestr += totalcmd;
            pipestr += "' to "+nickname[outputid]+" (#" +to_string(outputid)+") ***";
            broadcast(pipestr);
        }else{
            errmsg(fd,"*** Error: the pipe #"+to_string(id)+"->#"+to_string(outputid)+" already exists. ***");
            erroruser = true;
        }
    }
    return erroruser;
}
