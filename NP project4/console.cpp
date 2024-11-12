#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <sys/wait.h>
#include <stdlib.h>
#include <unistd.h>
#include <boost/asio.hpp>
#include <boost/iostreams/stream.hpp>
#include<fstream>

using boost::asio::ip::tcp;
using namespace boost::asio::ip;
using namespace boost::asio;
using namespace std;


class client : public std::enable_shared_from_this<client>
{
public:
  client(boost::asio::io_context &io,int id,string url,string port,string filename,string sh,string sp): 
  socket_(io),
  resolv_(io),
  id(id),
  url(url),
  port(port),
  sh(sh),
  sp(sp)
  {
    fin.open("./test_case/"+filename);
  }

  void start()
  {
    resolve_handler();
  }

  void outputfile(int num,string tmp){
    cout<<"<script>document.getElementById('s"<<num<<"').innerHTML += '"<<flush;
    tmp = replace_to_html(tmp);
    cout<<tmp<<flush;
    cout<<"<br>"<<flush;
    cout<<"';</script>"<<endl; 
  }

  void outputshell(int num,string str){
    str = replace_to_html(str);
    cout<<"<script>document.getElementById('s"<<num<<"').innerHTML += '"<<flush;
    cout<<str<<flush;
    cout<<"';</script>"<<endl; 
  }


private:
  void do_read(){
    usleep(200);
    auto self(shared_from_this());
    bzero(data_,max_length);
    socket_.async_read_some(buffer(data_,max_length),[this,self](boost::system::error_code ec, size_t length){
      if(!ec){
        string tmp = data_;
        if(tmp.find('%')!= string::npos){
          string ins;
          getline(fin,ins);
          tmp = tmp + ins;
          outputfile(id,tmp);
          do_write(ins);
        }else{
          outputshell(id,tmp);
          do_read();
        }
      }
    });
  }

  void do_write(string cmd){
    auto self(shared_from_this());
    cmd += "\n";
    boost::asio::async_write(socket_,boost::asio::buffer(cmd.c_str(),cmd.length()),[this,self,cmd](boost::system::error_code ec, size_t /*length*/){
      if(!ec){
        if(cmd == "exit\n"){
          socket_.close();
        }else{
          do_read();
        }
      }
    });
  }

  void resolve_handler(){
    auto self(shared_from_this());
    tcp::resolver::query q(sh,sp);
    resolv_.async_resolve(q,[this,self](boost::system::error_code ec,tcp::resolver::iterator it){
      if(!ec){
        connect_handler(it);
      }
    });
  }

  void connect_handler(ip::tcp::resolver::iterator it){
    auto self(shared_from_this());
    socket_.async_connect(*it,[this,self](const boost::system::error_code ec){
      if(!ec){
        do_socks();
        do_read();
      }
    });
  }

  string replace_to_html(string str){
    string htmlstr;
    for(int i=0;i<str.length();i++){
      if(str[i]=='\n'){
        htmlstr += "<br>";
      }else if(str[i]=='\r'){
        htmlstr += "";
      }else if(str[i]=='\''){
        htmlstr += "&apos;";
      }else if(str[i]=='\"'){
        htmlstr += "&quot;";
      }else if(str[i]=='&'){
        htmlstr += "&amp;";
      }else if(str[i]=='<'){
        htmlstr += "&lt;";
      }else if(str[i]=='>'){
        htmlstr += "&gt;";
      }else{
        htmlstr += str[i];
      }
    }
    return htmlstr;
  }

  void do_socks(){
    string req;
    unsigned char reply[8];
    req.push_back(4);
    req.push_back(1);
    int p = stoi(port);
    int req2 = p / 256;
    int req3 = p % 256;
    if(req2 > 128){
      req.push_back(req2-256);
    }else{
      req.push_back(req2);
    }
    if(req3 > 128){
      req.push_back(req3-256);
    }else{
      req.push_back(req3);
    }
    req.push_back(0);
    req.push_back(0);
    req.push_back(0);
    req.push_back(1);
    req.push_back(0);//null bit
    for (int i=0;i<url.length();i++){
      req.push_back(url[i]);
    }
    req.push_back(0); //null bit

    boost::asio::write(socket_, boost::asio::buffer(req), boost::asio::transfer_all());
    boost::asio::read(socket_, boost::asio::buffer(reply), boost::asio::transfer_all());

    if(reply[1] != 90){
      socket_.close();
    }
  }


  int id;
  tcp::socket socket_;
  tcp::resolver resolv_;
  string url;
  string port;
  string sh;
  string sp;
  enum { max_length = 4096 };
  char data_[max_length];
  ifstream fin;
};



class html{
public:
  html(string querystr){
    nums = 0;
    while(querystr.find("&")!=string::npos){
      if(nums<5){
        string tmp = querystr.substr(0,querystr.find("&"));
        host[nums] = tmp.substr(3);
        querystr = querystr.substr(querystr.find("&")+1);
        tmp = querystr.substr(0,querystr.find("&"));
        port[nums] = tmp.substr(3);
        querystr = querystr.substr(querystr.find("&")+1);
        tmp = querystr.substr(0,querystr.find("&"));
        file[nums] = tmp.substr(3);
        querystr = querystr.substr(querystr.find("&")+1);
        nums++;
      }else{
        string tmp = querystr.substr(0,querystr.find("&"));
        sh = tmp.substr(3);
        querystr = querystr.substr(querystr.find("&")+1);
        tmp = querystr.substr(0,querystr.find("&"));
        sp = tmp.substr(3);
        querystr = querystr.substr(querystr.find("&")+1);
        for(int i=0;i<5;i++){
          if(host[i]==""){
            nums = i-1;
            break;
          }
          nums = 4;
        }
      }
    }
    buildhtml(nums);
  }

  void buildhtml(int num){
    cout << "Content-type: text/html\r\n\r\n";
    cout<<"\
    <!DOCTYPE html>\
        <html lang=\"en\">\
        <head>\
            <meta charset=\"UTF-8\" />\
            <title>NP Project 3 Sample Console</title>\
            <link\
                rel=\"stylesheet\"\
                href=\"https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css\"\
                integrity=\"sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2\"\
                crossorigin=\"anonymous\"\
            />\
            <link\
                href=\"https://fonts.googleapis.com/css?family=Source+Code+Pro\"\
                rel=\"stylesheet\"\
            />\
            <link\
                rel=\"icon\"\
                type=\"image/png\"\
                href=\"https://cdn0.iconfinder.com/data/icons/small-n-flat/24/678068-terminal-512.png\"\
            />\
            <style>\
            * {\
                font-family: 'Source Code Pro', monospace;\
                font-size: 1rem !important;\
            }\
            body {\
                background-color: #212529;\
            }\
            pre {\
                color: #cccccc;\
            }\
            b {\
                color: #01b468;\
            }\
            </style>\
        </head>\
        <body>\
            <table class=\"table table-dark table-bordered\">\
            <thead>\
                <tr>"<<flush;
                for(int i=0;i<=nums;i++){
                  cout<<"<th scope=\"col\">"<<host[i]<<":"<<port[i]<<"</th>"<<flush;
                }
                cout<<"</tr>\
            </thead>\
            <tbody>\
                <tr>"<<flush;
                for(int i=0;i<=nums;i++){
                  cout<<"<td><pre id=\"s"<<i<<"\" class=\"mb-0\"></pre></td>"<<flush;
                }
                cout<<"</tr>\
            </tbody>\
            </table>\
        </body>\
        </html>"<<flush;
  }


  int nums;
  string host[5];
  string port[5];
  string file[5];
  string sh;
  string sp;

private:
};

int main(int argc, char* argv[])
{
  io_context io;
  try{
    html ht(getenv("QUERY_STRING"));
    for(int i=0;i<=ht.nums;i++){
      make_shared<client>(io,i,ht.host[i],ht.port[i],ht.file[i],ht.sh,ht.sp)->start();  
    }
    io.run();
  }
  catch(exception& e){
    cerr << "Exception: " << e.what() << "\n";
  }
  return 0;
}
