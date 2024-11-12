#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <sys/wait.h>
#include <stdlib.h>
#include <unistd.h>
#include <boost/asio.hpp>

using boost::asio::ip::tcp;
using namespace std;
int saveinfd = dup(STDIN_FILENO);
int saveoutfd = dup(STDOUT_FILENO);

void reaper(int sig){
  waitpid(-1,nullptr,WNOHANG);
}

class session : public std::enable_shared_from_this<session>
{
public:
  session(tcp::socket socket): socket_(std::move(socket))
  {
    target[0] = "REQUEST_METHOD";
    target[1] = "REQUEST_URI";
    target[2] = "QUERY_STRING";
    target[3] = "SERVER_PROTOCOL";
    target[4] = "HTTP_HOST";
    target[5] = "SERVER_ADDR"; 
    target[6] = "SERVER_PORT"; 
    target[7] = "REMOTE_ADDR"; 
    target[8] = "REMOTE_PORT";   
  }

  void start()
  {
    do_read();
  }

private:
  void do_read()
  {
    auto self(shared_from_this());
    socket_.async_read_some(boost::asio::buffer(data_, max_length),
        [this, self](boost::system::error_code ec, std::size_t length)
        {
          if (!ec)
          {
            string httpcontent = data_;
            dup2(socket_.native_handle(),STDOUT_FILENO);
            cout<<"HTTP/1.1 200 OK"<<endl;
            dup2(saveoutfd,STDOUT_FILENO);
            parsehttp(httpcontent);
            int pid;
            while((pid = fork()) <0);
            if(pid == 0){
                char* argv[1];
                argv[0] = nullptr;
                for (int i=0;i<9;i++){
                    setenv(target[i].c_str(),envvar[i].c_str(),1);
                }
                dup2(socket_.native_handle(),STDOUT_FILENO);
                socket_.close();
                string req;
                if(envvar[1].find("?")!=string::npos){
                    req = "."+envvar[1].substr(0,envvar[1].find("?"));
                }else{
                    req = "."+envvar[1];
                }
                if(execv(req.c_str(),argv)<0){
                    dup2(saveoutfd,STDOUT_FILENO);
                }
                exit(-1);
            }else{
                socket_.close();
            }
          }
        });
  }

  void parsehttp(string httpreq){
    int start = 0;
    int end = httpreq.find(" ");
    envvar[0] = httpreq.substr(start,end); //GET
    httpreq = httpreq.substr(end+1);
    end = httpreq.find(" ");
    envvar[1] = httpreq.substr(start,end); // /?AAA
    httpreq = httpreq.substr(end+1);
    end = httpreq.find("\r\n");
    if(envvar[1].find("?")!=string::npos){
        envvar[2] = envvar[1].substr(envvar[1].find("?")+1);// AAA
    }
    else{
        envvar[2] = "";
    }
    envvar[3] = httpreq.substr(start,end); // HTTP1.1
    httpreq = httpreq.substr(end+1);
    end = httpreq.find(" ");
    httpreq = httpreq.substr(end+1);
    end = httpreq.find("\r\n");
    envvar[4] = httpreq.substr(start,end); //192.168.56.110:7000 
    envvar[5] = socket_.local_endpoint().address().to_string();
    envvar[6] = to_string(socket_.local_endpoint().port());
    envvar[7] = socket_.remote_endpoint().address().to_string();
    envvar[8] = to_string(socket_.remote_endpoint().port());
  }




  tcp::socket socket_;
  enum { max_length = 1024 };
  char data_[max_length];
  string target[9];
  string envvar[9];
};

class server
{
public:
  server(boost::asio::io_context& io_context, short port)
    : acceptor_(io_context, tcp::endpoint(tcp::v4(), port))
  {
    do_accept();
  }

private:
  void do_accept()
  {
    acceptor_.async_accept(
        [this](boost::system::error_code ec, tcp::socket socket)
        {
          if (!ec)
          {
            std::make_shared<session>(std::move(socket))->start();
          }

          do_accept();
        });
  }

  tcp::acceptor acceptor_;
};

int main(int argc, char* argv[])
{
  (void) signal(SIGCHLD, reaper);
  try
  {
    if (argc != 2)
    {
      std::cerr << "Usage: async_tcp_echo_server <port>\n";
      return 1;
    }

    boost::asio::io_context io_context;

    server s(io_context, std::atoi(argv[1]));

    io_context.run();
  }
  catch (std::exception& e)
  {
    std::cerr << "Exception: " << e.what() << "\n";
  }

  return 0;
}