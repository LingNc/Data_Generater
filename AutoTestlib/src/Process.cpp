#include "Process.h"
#include "sysapi.h"
#include <sstream>
#include <sys/resource.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

namespace process{
    // Args类实现
    Args::Args(){}

    Args::Args(const std::vector<string> &arguments): _args(arguments){
        prepare_c_args();
    }

    Args::Args(const string &command){
        parse(command);
        prepare_c_args();
    }


    Args &Args::add(const string &arg){
        _args.push_back(arg);
        prepare_c_args();
        return *this;
    }

    Args &Args::add(const std::vector<string> &arguments){
        _args.insert(_args.end(),arguments.begin(),arguments.end());
        prepare_c_args();
        return *this;
    }

    Args &Args::set_program_name(const string &name){
        if(_args.empty()){
            _args.push_back(name);
        }
        else{
            _args[0]=name;
        }
        prepare_c_args();
        return *this;
    }

    void Args::clear(){
        _args.clear();
        _c_args.clear();
    }

    size_t Args::size() const{
        return _args.size();
    }

    char **Args::data(){
        prepare_c_args();
        return _c_args.data();
    }

    std::vector<string> Args::get_args() const{
        return _args;
    }

    string Args::get_program_name() const{
        if(!_args.empty()){
            return _args[0];
        }
        return "";
    }

    string &Args::operator[](size_t index){
        return _args[index];
    }

    const string &Args::operator[](size_t index) const{
        return _args[index];
    }

    void Args::prepare_c_args(){
        _c_args.clear();
        for(const auto &arg:_args){
            _c_args.push_back(const_cast<char *>(arg.c_str()));
        }
        _c_args.push_back(nullptr); // execvp需要以nullptr结尾
    }

    // 新增：解析命令行字符串
    Args &Args::parse(const string &command_line){
        enum State{ NORMAL,IN_QUOTE,IN_DQUOTE };
        State state=NORMAL;
        string current_arg;
        bool escaped=false;

        _args.clear(); // 清除旧的参数

        for(char c:command_line){
            if(escaped){
                // 处理转义字符 - 把原始的反斜杠和字符都添加到参数中
                current_arg+='\\';
                current_arg+=c;
                escaped=false;
                continue;
            }

            switch(state){
            case NORMAL:
                if(c=='\\'){
                    escaped=true;
                }
                else if(c=='\''){
                    current_arg+=c;  // 将单引号添加到参数中
                    state=IN_QUOTE;
                }
                else if(c=='\"'){
                    current_arg+=c;  // 将双引号添加到参数中
                    state=IN_DQUOTE;
                }
                else if(std::isspace(c)){
                    if(!current_arg.empty()){
                        _args.push_back(current_arg);
                        current_arg.clear();
                    }
                }
                else{
                    current_arg+=c;
                }
                break;

            case IN_QUOTE:
                if(c=='\\'){
                    escaped=true;  // 允许在单引号内转义
                }
                else{
                    current_arg+=c;  // 将所有字符添加到参数中，包括单引号
                    if(c=='\''&&!escaped){
                        state=NORMAL;
                    }
                }
                break;

            case IN_DQUOTE:
                if(c=='\\'){
                    escaped=true;  // 允许在双引号内转义
                }
                else{
                    current_arg+=c;  // 将所有字符添加到参数中，包括双引号
                    if(c=='\"'&&!escaped){
                        state=NORMAL;
                    }
                }
                break;
            }
        }

        // 处理最后一个参数
        if(!current_arg.empty()){
            _args.push_back(current_arg);
        }

        prepare_c_args();
        return *this;
    }

    // 计时器类
    Timer::~Timer(){
        stop();
    }

    void Timer::start(int timeout_ms,std::function<void()> callback){
        stop();
        running=true;
        thread=std::thread([this,timeout_ms,callback](){
            // 使用 sleep_for 等待指定时间
            std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
            if(running){
                callback();
            }
            });
    }

    void Timer::stop(){
        running=false;
        if(thread.joinable()){
            thread.join();
        }
    }

    // 进程类
    Process &Process::set_timeout(int timeout_ms){
        _timer.start(timeout_ms,[this](){
            if(is_running()){
                kill(SIGKILL);
                _status=TIMEOUT;
            }
            });
        return *this;
    }

    Process &Process::cancel_timeout(){
        _timer.stop();
        return *this;
    }

    Process &Process::set_memout(int memout_mb){
        _memsize=memout_mb;
        return *this;
    }

    Process &Process::cancel_memout(){
        _memsize=0;
        return *this;
    }

    void Process::init_pipe(){
        if(pipe(_stdin)==-1
            ||pipe(_stdout)==-1
            ||pipe(_stderr)==-1){
            throw std::runtime_error(name+":管道创建错误！");
        }
    }

    void Process::launch(const char arg[],char *args[]){
        _pid=fork();
        // 子进程
        if(_pid==0){
            // 设置环境变量
            for(const auto &[name,value]:_env_vars){
                setenv(name.c_str(),value.c_str(),1);
            }

            // 限制内存大小
            if(_memsize!=0){
                struct rlimit rl;
                rl.rlim_cur=_memsize*1024*1024; // 软限制
                rl.rlim_max=_memsize*1024*1024; // 硬限制

                if(setrlimit(RLIMIT_AS,&rl)==-1){
                    perror("setrlimit failed");
                    exit(EXIT_FAILURE);
                }
            }

            // 输入输出重定向
            dup2(_stdin[0],STDIN_FILENO);
            dup2(_stdout[1],STDOUT_FILENO);
            dup2(_stderr[1],STDERR_FILENO);

            // 关闭管道
            close_pipe(1);

            // 运行子程序
            execvp(arg,args);
            exit(EXIT_FAILURE);
        }
        else if(_pid<0){
            _status=ERROR;
            throw std::runtime_error(name+":子程序运行失败！");
        }
        _status=RUNNING;
        close_pipe(0);
    }

    void Process::close_pipe(bool flag){
        // 确保先关闭读端再关闭写端
        if(_stdin[flag]!=-1){
            ::close(_stdin[flag]);
            _stdin[flag]=-1;
        }
        if(_stdout[!flag]!=-1){
            ::close(_stdout[!flag]);
            _stdout[!flag]=-1;
        }
        if(_stderr[!flag]!=-1){
            ::close(_stderr[!flag]);
            _stderr[!flag]=-1;
        }
    }

    // 修改save_args方法使用Args类
    void Process::save_args(std::vector<string> &args){
        _args.clear(); // 清除旧的参数
        _args.add(args);
    }

    Process::Process(){}

    Process::Process(const string &path,const Args &args): _args(args){
        _path=path;
        if(args.size()>0){
            name=args.get_program_name();
        }
    }

    void Process::load(const string &path,const Args &args){
        _path=path;
        _args=args;
        if(args.size()>0){
            name=args.get_program_name();
        }
    }

    void Process::start(){
        init_pipe();
        launch(_path.c_str(),_args.data());
    }

    JudgeCode Process::wait(){
        int status;
        waitpid(_pid,&status,0);
        _exit_code=status;
        _pid=-1;
        if(_status==TIMEOUT){
            return TimeLimitEXceeded;
        }
        else if(WIFEXITED(status)){
            int temp=WEXITSTATUS(status);
            if(temp==0){
                _status=STOP;
                return Waiting;
            }
            else{
                _status=ERROR;
                return Waiting;
            }
        }
        else if(WIFSIGNALED(status)){
            int signal=WTERMSIG(status);
            _status=RE;
            switch(signal){
            case SIGSEGV:
                return RuntimeError;
                break;
            case SIGABRT||SIGKILL:
                return MemoryLimitExceeded;
                break;
            case SIGFPE:
                return FloatingPointError;
                break;
            default:
                return RuntimeError;
            }
        }
        else{
            _status=RE;
            return RuntimeError;
        }
    }

    int Process::get_exit_code() const{
        return _exit_code;
    }

    Process &Process::write(const string &data){
        if(_stdin[1]!=-1){
            if(::write(_stdin[1],data.c_str(),data.size())==-1){
                throw std::runtime_error(name+":进程读取错误！");
            }
        }

        return *this;
    }

    string Process::read(TypeOut type){
        int stdpipe=(type==OUT)?_stdout[0]:_stderr[0];
        if(stdpipe==-1) return "";

        char buffer[_buffer_size];
        string result;

        // 设置非阻塞模式以避免无限等待
        _sys.start_blocked(stdpipe);


        int nbytes;
        while(true){
            nbytes=::read(stdpipe,buffer,_buffer_size-1);
            if(nbytes<=0) break;
            buffer[nbytes]='\0';
            result+=buffer;
        }

        // 恢复阻塞模式
        _sys.close_blocked(stdpipe);

        _empty=(result=="")?true:false;
        return result;
    }

    char Process::read_char(TypeOut type){
        // 获取合适的管道文件描述符
        int stdpipe=(type==OUT)?_stdout[0]:_stderr[0];
        if(stdpipe==-1) return '\0';

        // 设置非阻塞模式以避免无限等待
        _sys.start_blocked(stdpipe);

        char ch;
        int nbytes;
        nbytes=::read(stdpipe,&ch,1);

        // 恢复阻塞模式
        _sys.close_blocked(stdpipe);

        if(nbytes>0) return ch;
        else return 0;
    }

    string Process::read_line(TypeOut type){
        // 获取合适的管道文件描述符
        int stdpipe=(type==OUT)?_stdout[0]:_stderr[0];
        if(stdpipe==-1) return "";

        // 设置非阻塞模式以避免无限等待
        _sys.start_blocked(stdpipe);

        string line;
        char c;
        // 有数据可读，开始读取一行数据
        while(::read(stdpipe,&c,1)>0){
            // 遇到换行符结束
            if(c=='\n') break;
            // 将字符添加到结果中
            line+=c;
        }
        _empty=(line=="")?true:false;


        // 恢复阻塞模式
        _sys.close_blocked(stdpipe);

        return line;
    }

    char Process::getc(){
        return read_char(OUT);
    }

    string Process::getline(){
        return read_line(OUT);
    }

    bool Process::empty(){
        return _empty;
    }

    void Process::set_block(bool status){
        _sys.set_blocked(status);
    }

    void Process::close(){
        close_pipe(1);
        close_pipe(0);
    }

    bool Process::kill(int signal){
        if(_pid<=0){
            // 程序已经结束
            return false;
        }
        // 发送终止信号
        int result=::kill(_pid,signal);

        if(result==0){
            // 发送成功
            if(signal==SIGKILL||signal==SIGTERM){
                // 等待进程结束
                wait();
                // 关闭管道
                close();
                _status=STOP;
            }
            return true;
        }
        return false;
    }

    // 新增：检查进程是否在运行
    bool Process::is_running(){
        if(_pid<=0){
            _status=STOP;
            return false;
        }

        // 发送信号0检查进程是否存在
        int result=::kill(_pid,0);

        if(result==0){
            // 进程存在
            _status=RUNNING;
            return true;
        }
        else{
            // 检查错误类型
            if(errno==ESRCH){
                // 进程不存在
                _pid=-1;
                _status=STOP;
                return false;
            }
            else if(errno==EPERM){
                // 没有权限，但进程可能存在
                _status=RUNNING;
                return true;
            }
        }

        // 其他错误情况
        _status=ERROR;
        return false;
    }

    Process &Process::operator<<(std::ostream &(*pf)(std::ostream &)){
        if(pf==static_cast<std::ostream&(*)(std::ostream &)>(std::endl)){
            write("\n");
            flush();
        }
        return *this;
    }

    Process &Process::flush(){
        return *this;
    }

    Process::~Process(){
        if(is_running()){
            kill(SIGTERM);
        }
        close();
        close_pipe(1);
        wait();
    }

    Process &Process::set_env(const std::string &name,const std::string &value){
        _env_vars[name]=value;
        return *this;
    }

    std::string Process::get_env(const std::string &name) const{
        auto it=_env_vars.find(name);
        if(it!=_env_vars.end()){
            return it->second;
        }
        // 尝试获取当前进程的环境变量
        const char *val=getenv(name.c_str());
        return val?val:"";
    }

    void Process::unset_env(const std::string &name){
        _env_vars.erase(name);
    }

    void Process::clear_env(){
        _env_vars.clear();
    }
}