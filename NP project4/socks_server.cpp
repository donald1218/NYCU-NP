#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <sys/wait.h>
#include <stdlib.h>
#include <unistd.h>
#include <boost/asio.hpp>
#include <fstream>
#include <vector>

using boost::asio::ip::tcp;
using namespace std;

struct socks4req{
	int VN;
	int CD;
	string srcIP;
	string srcPort;
	string dstIP;
	string dstPort;
	string command;
	string reply;
};

class session : public std::enable_shared_from_this<session>
{
public:
  session(tcp::socket socket,boost::asio::io_context& io_context): 
  socket_1(std::move(socket)),
  socket_2(io_context),
  io_context_(io_context)
  {
  }

  void start()
  {
    do_read();
  }

private:
  void do_read()
  {
    auto self(shared_from_this());
    memset(data_, '\0', sizeof(data_));
    socket_1.async_read_some(boost::asio::buffer(data_, max_length),
        [this, self](boost::system::error_code ec, std::size_t length)
        {
          if (!ec)
          {
			parse();
			cout << "<S_IP>: " << socks4req_.srcIP <<endl;
			cout << "<S_PORT>: " << socks4req_.srcPort <<endl;
			cout << "<D_IP>: " << socks4req_.dstIP <<endl;
			cout << "<D_PORT>: " << socks4req_.dstPort <<endl;
			cout << "<Command>: " << socks4req_.command <<endl;
			cout << "<Reply>: " << socks4req_.reply <<endl;
			socks4reply[0] = 0;
			if(socks4req_.reply == "Accept"){
				socks4reply[1] = 90;
				if(socks4req_.CD == 1){
					do_connect();
				}else{
					do_bind();
				}
			}else{
				socks4reply[1] = 91;
				do_write_reply();
				exit(0);
			}
          }
        });
  }

	void parse(){
		for(int i = 2; i < 8; i++){
			socks4reply[i] = 0;
			socks4reply[i] = data_[i];
		}
		socks4req_.VN = data_[0];
		if(socks4req_.VN != 4){
			socks4req_.reply = "Reject";
		}
		socks4req_.CD = data_[1];
		if(socks4req_.CD == 1){
			socks4req_.command = "CONNECT";
		}else if(socks4req_.CD == 2){
			socks4req_.command = "BIND";
		}else{
			socks4req_.command = "UNKNOW";
			socks4req_.reply = "Reject";
		}
		socks4req_.dstPort = to_string((unsigned int)(data_[2] << 8 ) | data_[3]);
		if(issock4a()){
			int index = 8;
			while(data_[index] != 0){ //userid
				index++;
			} 
			index++; //null bit
			string domain_name;
			while(data_[index] != 0){
				domain_name.push_back(data_[index]);
				index ++;
			}
			tcp::resolver resolver_(io_context_);
			tcp::endpoint endpoint_ = resolver_.resolve(domain_name, socks4req_.dstPort)->endpoint();
			socks4req_.dstIP = endpoint_.address().to_string();
		}else{
			char dstIP[20];
			snprintf(dstIP, 20, "%d.%d.%d.%d", data_[4], data_[5], data_[6], data_[7]);
			socks4req_.dstIP = string(dstIP);
		}
		socks4req_.srcIP = socket_1.remote_endpoint().address().to_string();
		socks4req_.srcPort = to_string(socket_1.remote_endpoint().port());
		if(!firewall()){
			socks4req_.reply = "Reject";
		}else{
			socks4req_.reply = "Accept";
		}
  	}

	bool issock4a(){
		if(data_[4]==0&&data_[5]==0&&data_[6]==0&&data_[7]!=0)
			return true;	
		return false;
	}

	bool firewall(){
		if(socks4req_.reply == "Reject"){
			return false;
		}
		ifstream fin("./socks.conf");
		if(!fin.is_open()){
			return false;
		}
		string rule;
		while(getline(fin,rule)){
			string op = rule.substr(0,rule.find(" "));
			rule = rule.substr(rule.find(" ")+1);
			string type = rule.substr(0,rule.find(" "));
			rule = rule.substr(rule.find(" ")+1);
			string IP[4];
			for(int i=0;i<4;i++){
				IP[i] = rule.substr(0,rule.find("."));
				rule = rule.substr(rule.find(".")+1);
			}
			if(op == "permit"){
				if(type == "c" && socks4req_.CD == 2){ //req with bind
					continue;
				}
				if(type == "b" && socks4req_.CD == 1){ // req with connect
					continue;
				}
				vector<string> dstIP;
				string tmp = socks4req_.dstIP;
				for(int i=0;i<4;i++){
					dstIP.push_back(tmp.substr(0,tmp.find(".")));
					tmp = tmp.substr(tmp.find(".")+1);
				}
				bool accept = true;
				for(int i=0;i<4;i++){
					if(IP[i] == "*"){
						continue;
					}
					if(IP[i] != dstIP[i]){
						accept = false;
						break;
					}
				}
				if(accept){
					return true;
				}
			}
		}
		return false;
	}

	void do_write_reply(){
		auto self(shared_from_this());
		boost::asio::async_write(
			socket_1, boost::asio::buffer(socks4reply, 8),
			[this, self](boost::system::error_code ec, size_t length){
				if(!ec){
					
				}
			}
		);
	}

	void do_connect(){
		auto self(shared_from_this());
		tcp::resolver resolver_(io_context_);
		tcp::resolver::results_type endpoint_ = resolver_.resolve(socks4req_.dstIP, socks4req_.dstPort);
		boost::asio::async_connect(
			socket_2, endpoint_,
			[this, self](boost::system::error_code ec, tcp::endpoint ed){
				if(!ec){
					do_write_reply();
					do_read_from(socket_1,socket_2);
					do_read_from(socket_2,socket_1);
				}else{
					socks4reply[1] = 91;
					do_write_reply();
					socket_1.close();
					socket_2.close();
				}
			}
		);
	}

	void do_bind(){
		tcp::acceptor acceptor_(io_context_, tcp::endpoint(tcp::v4(), 0));
		acceptor_.listen();
		unsigned int p = acceptor_.local_endpoint().port();
		socks4reply[2] = (p >> 8) & 0xFF;
		socks4reply[3] = p & 0xFF;
		socks4reply[4] = 0;
		socks4reply[5] = 0;
		socks4reply[6] = 0;
		socks4reply[7] = 0;
		do_write_reply();
		acceptor_.accept(socket_2);
		acceptor_.close();
		do_write_reply();
		do_read_from(socket_1,socket_2);
		do_read_from(socket_2,socket_1);
	}

	void do_read_from(tcp::socket socksrc,tcp::socket sockdst){
		auto self(shared_from_this());
		memset(data_, '\0', sizeof(data_));
		socksrc.async_read_some(
			boost::asio::buffer(data_, max_length), 
			[this, self](boost::system::error_code ec, std::size_t length){
				if (!ec){
					do_write_to(sockdst,socksrc,length);	
				}else{
					socket_1.close();
					socket_2.close();
					exit(0);
				}
			}
		);	
	}

	void do_write_to(tcp::socket sockdst,tcp::socket socksrc,size_t length){
		auto self(shared_from_this());
		boost::asio::async_write(
			sockdst, boost::asio::buffer(data_, length),
			[this, self](boost::system::error_code ec, size_t length){
				if(!ec){
					do_read_from(sockdst,socksrc);
				}
			}
		);
	}


	tcp::socket socket_1;
	tcp::socket socket_2;
	boost::asio::io_context& io_context_;
	struct socks4req socks4req_;
	enum { max_length = 1024 };
	unsigned char data_[max_length];
	unsigned char socks4reply[8];
};

class server
{
public:
	server(boost::asio::io_context& io_context, short port): 
	acceptor_(io_context, tcp::endpoint(tcp::v4(), port)),
	io_context_(io_context),
	signal_(io_context, SIGCHLD)
	{
		acceptor_.set_option(boost::asio::socket_base::reuse_address(true));
		sig_handler();
		do_accept();
	}

private:
	void sig_handler(){
		signal_.async_wait(
			[this](boost::system::error_code ec, int signo){
				if(acceptor_.is_open()){
					int state;
					while(waitpid(-1, &state, WNOHANG) > 0);
					sig_handler();
				}
			}
		);
	}

	void do_accept()
	{
		acceptor_.async_accept(
			[this](boost::system::error_code ec, tcp::socket socket)
			{
				if (!ec)
				{
					io_context_.notify_fork(boost::asio::io_context::fork_prepare);
					pid_t pid = fork();
					while(pid <0){
						pid = fork();
					}
					if(pid == 0){
						io_context_.notify_fork(boost::asio::io_context::fork_child);
						acceptor_.close();
						signal_.cancel();
						std::make_shared<session>(std::move(socket), io_context_)->start();
					}else{
						io_context_.notify_fork(boost::asio::io_context::fork_parent);
						socket.close();
					}
				}
				do_accept();
			});
	}

	tcp::acceptor acceptor_;
	boost::asio::io_context& io_context_;
	boost::asio::signal_set signal_;
};

int main(int argc, char* argv[])
{
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
