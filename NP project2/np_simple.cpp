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
using namespace std;
int child_num = 0;

int execute_cmd(vector<char*> tmp,int* pipein,int* pipeout,int type);
int redirect(char* cmd,char* filename,int* pipein,int fd);
void clean_queue(int ins,int numpipe,vector<int> &nonwaitpid);
int npshell();
map<int,int*> numberpiped;
queue<vector<char*>> command;
queue<int> type;//0:non 1:| 2:! 3:>
int ins=0;

void perr(string str,int err_no){
    cout<<str<<endl<<"errno "<<strerror(err_no)<<endl;
    exit(-1);
}


int main(int argc,char* argv[]){
    int sockfd,newsockfd,childpid;
    socklen_t clilen;
    int iopt = 1;
    struct sockaddr_in serv_addr,cli_addr;
    if((sockfd = socket(AF_INET,SOCK_STREAM,0))<0){
        cout<<("server:can't open stream socket")<<endl;
        exit(-1);
    }
    bzero((char*)&serv_addr,sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(atoi(argv[1]));
    if(setsockopt(sockfd,SOL_SOCKET,SO_REUSEADDR,&iopt,sizeof(iopt))<0){
        perr("setsockopt error",errno);
    }
    if(bind(sockfd,(struct sockaddr*)&serv_addr,sizeof(serv_addr))<0){
        perr("server:can't bind local address",errno);
    }
    listen(sockfd,5);
    while(true){
        clilen = sizeof(cli_addr);
        newsockfd = accept(sockfd,(struct sockaddr *)&cli_addr,&clilen);
        if(newsockfd<0){
            cout<<("server accept error")<<endl;
            exit(-1);
        }
        childpid = fork();
        while(childpid<0){
            wait(nullptr);
            childpid = fork();
        }
        if(childpid==0){
            dup2(newsockfd,STDIN_FILENO);
            dup2(newsockfd,STDOUT_FILENO);
            dup2(newsockfd,STDERR_FILENO);
            npshell();
            exit(0);
        }else{
            close(newsockfd);
            wait(nullptr);
        }
    }
}

int npshell(){
    char input[15000];
    setenv("PATH","bin:.",1);
    int pipecnt = 0;
    char* cmd;
    char* path;
    bool first_cmd = true;
    bool has_numpipe = false;
    int pid_c;
    int pipes[1000][2];
    vector<int> nonwaitpid;
    queue<int> pids;
    while(1){
        pipecnt = 0;
        first_cmd = true;
        for(int i=0;i<nonwaitpid.size();i++){
            if(waitpid(nonwaitpid[i],nullptr,WNOHANG)!=0){
                nonwaitpid.erase(nonwaitpid.begin()+i);
                child_num--;
            }
        }
        cout<<"% "<<flush;
        cin.getline(input,15000);
        if(strlen(input)==0){
            continue;
        }
        cmd = strtok(input," \r");
        if(cmd!=NULL){
            if(!has_numpipe){
                ins++;
            }else{
                has_numpipe = false;
            }
        }
        if(strcmp(cmd,"exit")==0){
            break;
        }else if(strcmp(cmd,"printenv")==0){
            char* target = strtok(NULL," \r");
            if(target != NULL && getenv(target)!=NULL){
                cout<<getenv(target)<<flush<<endl<<flush;
            }
            continue;
        }else if(strcmp(cmd,"setenv")==0){
            char* target = strtok(NULL," \r");
            char* env = strtok(NULL," \r");
            setenv(target,env,1);
            continue;
        }
        while(cmd!=NULL){
            vector<char*> tmp;
            char* cmds = cmd;
            cmd = strtok(NULL," \r");
            tmp.clear();
            tmp.push_back(cmds);
            if(cmd==NULL){
                command.push(tmp);
                type.push(0);
            }else{
                if(cmd[0]=='!'||cmd[0]=='|'||cmd[0]=='>'){
                    if(strlen(cmd)>1){
                        int numpipe=0;
                        for(int i=1;i<strlen(cmd);i++){
                            numpipe*=10;
                            numpipe+=(int)(cmd[i]-'0');
                        }
                        if(numpipe>1000){
                            numpipe=1000;
                        }
                        if(numpipe<1){
                            numpipe=1;
                        }
                        command.push(tmp);
                        if(cmd[0]=='|'){
                            type.push(1);
                        }else if(cmd[0]=='!'){
                            type.push(2);
                        }else{
                            type.push(3);
                        }
                        clean_queue(ins,numpipe,nonwaitpid);
                        first_cmd = true;
                        ins++;
                        has_numpipe=true;
                    }else{
                        command.push(tmp);
                        if(cmd[0]=='|'){
                            type.push(1);
                        }else if(cmd[0]=='!'){
                            type.push(2);
                        }else{
                            type.push(3);
                        }
                    }
                }else{
                    path = cmd;
                    tmp.push_back(path);
                    while(cmd[0]!='!'&&cmd[0]!='|'&&cmd[0] != '>'){
                        cmd = strtok(NULL," \r");
                        if(cmd==NULL){
                            break;
                        }
                        if(cmd[0]!='!'&&cmd[0]!='|'&&cmd[0] != '>'){
                            tmp.push_back(cmd);
                        }
                    }
                    if(cmd!=NULL){
                        if(strlen(cmd)>1){
                            int numpipe=0;
                            for(int i=1;i<strlen(cmd);i++){
                                numpipe*=10;
                                numpipe+=(int)(cmd[i]-'0');
                            }
                            if(numpipe>1000){
                                numpipe=1000;
                            }
                            if(numpipe<1){
                                numpipe=1;
                            }
                            command.push(tmp);
                            if(cmd[0]=='|'){
                                type.push(1);
                            }else if(cmd[0]=='!'){
                                type.push(2);
                            }else{
                                type.push(3);
                            }
                            clean_queue(ins,numpipe,nonwaitpid);
                            first_cmd = true;
                            has_numpipe = true;
                            ins++;
                        }else{
                            command.push(tmp);
                            if(cmd[0]=='|'){
                                type.push(1);
                            }else if(cmd[0]=='!'){
                                type.push(2);
                            }else{
                                type.push(3);
                            }
                        }
                    }else{
                        command.push(tmp);
                        type.push(0);
                    }
                }
            }
            cmd = strtok(NULL," \r");
        }
        while(!command.empty()){
            if(first_cmd){
                auto iter = numberpiped.find(ins);
                if(command.size()==1){
                    if(iter!=numberpiped.end()){
                        pid_c = execute_cmd(command.front(),iter->second,NULL,type.front());
                        pids.push(pid_c);
                        numberpiped.erase(iter);
                    }else{
                        pid_c = execute_cmd(command.front(),NULL,NULL,type.front());
                        pids.push(pid_c);
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
                        int fd = open(fn,O_CREAT|O_TRUNC|O_RDWR,S_IRWXU);
                        if(iter!=numberpiped.end()){
                            if(rcmd.size()>1){
                                pid_c = redirect(rcmd[0],rcmd[1],iter->second,fd);
                            }else{
                                pid_c = redirect(rcmd[0],NULL,iter->second,fd);
                            }
                            pids.push(pid_c);
                        }else{
                            if(rcmd.size()>1){
                                pid_c = redirect(rcmd[0],rcmd[1],NULL,fd);
                            }else{
                                pid_c = redirect(rcmd[0],NULL,NULL,fd);
                            }
                            pids.push(pid_c);
                        }
                    }else{
                        if(pipe(pipes[pipecnt%1000])<0){
                                cout<<"creat pipe error"<<endl<<flush;
                        }   
                        if(iter!=numberpiped.end()){
                            pid_c = execute_cmd(command.front(),iter->second,pipes[pipecnt%1000],type.front());
                            numberpiped.erase(iter);
                            pids.push(pid_c);
                        }else{
                            pid_c = execute_cmd(command.front(),NULL,pipes[pipecnt%1000],type.front());
                            pids.push(pid_c);
                        }
                        first_cmd = false;
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
                        int fd = open(fn,O_CREAT|O_TRUNC|O_RDWR,S_IRWXU);
                        if(rcmd.size()>1){
                            pid_c = redirect(rcmd[0],rcmd[1],pipes[(pipecnt-1)%1000],fd);
                        }else{
                            pid_c = redirect(rcmd[0],NULL,pipes[(pipecnt-1)%1000],fd);
                        }
                        pids.push(pid_c);
                    }else{
                        if(pipe(pipes[pipecnt%1000])<0){
                            cout<<"creat pipe error"<<endl<<flush;
                        }
                        pid_c = execute_cmd(command.front(),pipes[(pipecnt-1)%1000],pipes[pipecnt%1000],type.front());
                        pids.push(pid_c);
                    }
                }
            }
            pipecnt++;
            command.pop();
            type.pop();
            while(child_num>200){
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
    }
    return EXIT_SUCCESS;
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
        child_num--;
        pid = fork();
    }
    if(pid==0){
        int savefd;
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
            savefd = dup(STDOUT_FILENO);
            dup2(pipeout[1],STDOUT_FILENO);
        }
        if(execvp(argv[0],argv)<0){
            dup2(savefd,STDOUT_FILENO);
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

int redirect(char* cmd,char* filename,int* pipein,int fd){
    int pid = fork();
    while(pid==-1){
        waitpid(-1,nullptr,0);
        child_num--;
        pid = fork();
    }
    if(pid==0){
        int savefd;
        if(pipein!=NULL){
            close(pipein[1]);
            dup2(pipein[0],STDIN_FILENO);
        }
        savefd = dup(STDOUT_FILENO);
        dup2(fd,STDOUT_FILENO);
        if(execlp(cmd,cmd,filename,NULL)<0){
            dup2(savefd,STDOUT_FILENO);
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

void clean_queue(int ins,int numpipe,vector<int> &nonwaitpid){
    queue<int> pids;
    int pid_c;
    bool first_cmd = true;
    int pipecnt = 0;
    int pipes[1000][2];
    while(!command.empty()){
        if(first_cmd){
            if(command.size()==1){
                auto iter = numberpiped.find(ins+numpipe);
                auto iter2 = numberpiped.find(ins);
                if(iter == numberpiped.end()){
                    int* npipe = new int[2];
                    if(pipe(npipe)<0){
                        cout<<"creat pipe error"<<endl<<flush;
                    }
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
                continue;
            }
            if(pipe(pipes[pipecnt%1000])<0){
                cout<<"creat pipe error"<<endl<<flush;
            }          
            pid_c = execute_cmd(command.front(),NULL,pipes[pipecnt%1000],type.front());
            pids.push(pid_c);
            first_cmd = false;
        }else{
            if(command.size()==1){
                auto iter = numberpiped.find(ins+numpipe);
                if(iter == numberpiped.end()){
                    int *npipe = new int[2];
                    if(pipe(npipe)<0){
                        cout<<"creat pipe error"<<endl<<flush;
                    }
                    numberpiped[ins+numpipe]=npipe;
                    pid_c = execute_cmd(command.front(),pipes[(pipecnt-1)%1000],npipe,type.front());
                }else{
                    pid_c = execute_cmd(command.front(),pipes[(pipecnt-1)%1000],iter->second,type.front());
                }
                pids.push(pid_c);
            }else{
                if(pipe(pipes[pipecnt%1000])<0){
                    cout<<"creat pipe error"<<endl<<flush;
                }
                pid_c = execute_cmd(command.front(),pipes[(pipecnt-1)%1000],pipes[pipecnt%1000],type.front());
                pids.push(pid_c);
            }
        }
        pipecnt++;
        command.pop();
        type.pop();
        while(child_num>200){
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
            child_num--;
            if(n>3){
                child_num++;
                nonwaitpid.push_back(pids.front());
                break;
            }
        }
        pids.pop();
    }
    for(int i=0;i<nonwaitpid.size();i++){
        if(waitpid(nonwaitpid[i],nullptr,WNOHANG)!=0){
            nonwaitpid.erase(nonwaitpid.begin()+i);
            child_num--;
        }
    }
}
