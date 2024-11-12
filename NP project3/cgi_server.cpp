#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <stdlib.h>
#include <boost/asio.hpp>
#include <boost/iostreams/stream.hpp>
#include <fstream>

using namespace boost::asio::ip;
using namespace boost::asio;
using boost::asio::ip::tcp;
using namespace std;



class session : public std::enable_shared_from_this<session>
{
private: 
  class client : public std::enable_shared_from_this<client>
  {
  public:
    shared_ptr<session> ss;
    client(boost::asio::io_context &io,int id,string url,string port,string filename,shared_ptr<session> s): 
    socket_(io),
    resolv_(io),
    id(id),
    url(url),
    port(port),
    ss(s)
    {
      fin.open("./test_case/"+filename);
    }

    void start()
    {
      resolve_handler();
    }

    void outputfile(int num,string tmp){
      string output = "<script>document.getElementById('s" + to_string(num) + "').innerHTML += '";
      tmp = replace_to_html(tmp);
      output += tmp;
      output += "<br>";
      output += "';</script>\n";
      ss->do_write(output);
    }

    void outputshell(int num,string str){
      string output = "<script>document.getElementById('s" + to_string(num) + "').innerHTML += '";
      str = replace_to_html(str);
      output += str;
      output += "';</script>\n";
      ss->do_write(output);
    }


  private:
  void do_read(){
    usleep(200);
    auto self(shared_from_this());
    memset(data_,'\0', sizeof(data_));
    socket_.async_read_some(boost::asio::buffer(data_,max_length),[this,self](boost::system::error_code ec, size_t length){
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
    tcp::resolver::query q(url,port);
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


  int id;
  tcp::socket socket_;
  tcp::resolver resolv_;
  string url;
  string port;
  enum { max_length = 4096 };
  char data_[max_length];
  ifstream fin;
  
  };

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

  void do_write(string str){
    auto self(shared_from_this());
    boost::asio::async_write(socket_,boost::asio::buffer(str.c_str(),str.length()),
    [this,self](boost::system::error_code ec, size_t /*length*/){
      if(!ec){

      }
    });
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
            // do_write("HTTP/1.1 200 OK\r\n\r\n");
            parsehttp(httpcontent);
            cout<<envvar[2]<<endl;
            if(envvar[1].find("?")==string::npos){ //panel.cgi
              buildpanel();
            }else{ //console.cgi
              try{
                io_context io;
                parsequery(envvar[2]);
                for(int i=0;i<nums;i++){
                  make_shared<client>(io,i,host[i],port[i],file[i],self)->start();
                } 
                io.run(); 
              }
              catch(exception& e){
                cerr << "Exception: " << e.what() << "\n";
              }
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

  void buildconsole(int num){
    string console;
    console += "HTTP/1.1 200 OK\r\n\r\n";
    console += "<!DOCTYPE html>\n"																				;
    console += "<html lang=\"en\">\n"																			;
    console += " <head>\n"																						;
    console += "    <meta charset=\"UTF-8\" />\n"																;
    console += "    <title>NP Project 3 Sample Console</title>\n"												;
    console += "    <link\n"																					;
    console += "      rel=\"stylesheet\"\n"																		;
    console += "      href=\"https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css\"\n"		;
    console += "      integrity=\"sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2\"\n"	;
    console += "      crossorigin=\"anonymous\"\n"																;
    console += "    />\n"																						;
    console += "    <link\n"																					;
    console += "      href=\"https://fonts.googleapis.com/css?family=Source+Code+Pro\"\n"						;
    console += "      rel=\"stylesheet\"\n"																		;
    console += "    />\n"																						;
    console += "    <link\n"																					;
    console += "      rel=\"icon\"\n"																			;
    console += "      type=\"image/png\"\n"																		;
    console += "      href=\"https://cdn0.iconfinder.com/data/icons/small-n-flat/24/678068-terminal-512.png\"\n";
    console += "    />\n"																						;
    console += "    <style>\n"																					;
    console += "      * {\n"																					;
    console += "        font-family: 'Source Code Pro', monospace;\n"											;
    console += "        font-size: 1rem !important;\n"															;
    console += "      }\n"																						;
    console += "      body {\n"																					;
    console += "        background-color: #212529;\n"															;
    console += "      }\n"																						;
    console += "      pre {\n"																					;
    console += "        color: #cccccc;\n"																		;
    console += "      }\n"																						;
    console += "      b {\n"																					;
    console += "        color: #01b468;\n"																		;
    console += "      }\n"																						;
    console += "    </style>\n"																					;
    console += "  </head>\n"																					;
    console += "  <body>\n"																						;
    console += "    <table class=\"table table-dark table-bordered\">\n"										;
    console += "      <thead>\n"																				;
    console += "        <tr>\n"																					;
    for(int i = 0; i < num; i++){
      console += "          <th scope=\"col\">:" +host[i]+":"+port[i]+ "</th>"	;
    }
    console += "        </tr>\n"																				;
    console += "      </thead>\n"																				;
    console += "      <tbody>\n"																				;
    console += "        <tr>\n"																					;
    for(int i = 0; i < num; i++){
      console += "          <td><pre id=\"s" + to_string(i) + "\" class=\"mb-0\"></pre></td>\n"				;
    }
    console += "        </tr>\n"																				;
    console += "      </tbody>\n"																				;
    console += "    </table>\n"																					;
    console += "  </body>\n"																					;
    console += "</html>\n"                                            ;
    do_write(console);
  }

  void buildpanel(){
    string panel;
    panel += "HTTP/1.1 200 OK\r\n";
    panel += "Content-type: text/html\r\n\r\n";
    panel += "<!DOCTYPE html>";
    panel += "<head><title>NP Project 3 Panel</title><linkrel=\"stylesheet\"href=\"https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css\"integrity=\"sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2\"crossorigin=\"anonymous\"/><linkhref=\"https://fonts.googleapis.com/css?family=Source+Code+Pro\"rel=\"stylesheet\"/><linkrel=\"icon\"type=\"image/png\"href=\"https://cdn4.iconfinder.com/data/icons/iconsimple-setting-time/512/dashboard-512.png\"/><style>* {font-family: \'Source Code Pro\', monospace;}</style></head>";
    panel += "<body class=\"bg-secondary pt-5\">";
    panel += "<form action=\"console.cgi\" method=\"GET\"><table class=\"table mx-auto bg-light\" style=\"width: inherit\"><thead class=\"thead-dark\"><tr><th scope=\"col\">#</th><th scope=\"col\">Host</th><th scope=\"col\">Port</th><th scope=\"col\">Input File</th></tr></thead><tbody>";
    for(int i = 0;i < 5;i++){
      panel += "<tr>";
      panel += "<th scope=\"row\" class=\"align-middle\">Session "+to_string(i+1)+"</th>";
      panel += "<td><div class=\"input-group\"><select name=\"h"+to_string(i)+"\" class=\"custom-select\">";
      panel += "<option></option>";
      for(int j = 1;j <= 12;j++){
        panel += "<option value=\"nplinux"+to_string(j)+".cs.nycu.edu.tw\">nplinux"+to_string(j)+"</option>";
      }
      panel += "</select><div class=\"input-group-append\"><span class=\"input-group-text\">.cs.nycu.edu.tw</span></div></div></td>";
      panel += "<td><input name=\"p"+to_string(i)+"\" type=\"text\" class=\"form-control\" size=\"5\" /></td>";
      panel += "<td><select name=\"f"+to_string(i)+"\" class=\"custom-select\">";
      panel += "<option></option>";
      for(int j = 1;j <= 5;j++){
        panel += "<option value=\"t"+to_string(j)+".txt\">t"+to_string(j)+".txt</option>";
      }
      panel += "</select></td>";
    }
    panel += "<tr><td colspan=\"3\"></td><td><button type=\"submit\" class=\"btn btn-info btn-block\">Run</button></td></tr></tbody></table></form></body></html>";
    do_write(panel);
  }

  void parsequery(string querystr){
    nums = 0;
    while(querystr.find("&")!=string::npos){
      string tmp = querystr.substr(0,querystr.find("&"));
      if(tmp.length()==3){
        break;
      }
      host[nums] = tmp.substr(3);
      querystr = querystr.substr(querystr.find("&")+1);
      tmp = querystr.substr(0,querystr.find("&"));
      port[nums] = tmp.substr(3);
      querystr = querystr.substr(querystr.find("&")+1);
      tmp = querystr.substr(0,querystr.find("&"));
      file[nums] = tmp.substr(3);
      querystr = querystr.substr(querystr.find("&")+1);
      nums++;
    }
    buildconsole(nums);
  }

  int nums;
  string host[5];
  string port[5];
  string file[5];
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