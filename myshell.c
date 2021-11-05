#define _GNU_SOURCE
#include<signal.h>
#include<stdio.h>
#include<sys/stat.h>
#include<sys/types.h>
#include<sys/wait.h>
#include<unistd.h>
#include<stdlib.h>
#include<string.h>
#include<fcntl.h>
#include<pwd.h>
#define MAXLEN 1024
#define PIPESIZE 20

//运行cd，即切换工作目录
void run_cd(char* path)
{
    if(chdir(path) < 0)
        perror("chdir");
}

//运行命令
//实现功能：1.重定向输入输出 2.管道功能
void run_cmd(char* cmdvec[MAXLEN], int cmd_cnt, int is_bg)
{
    //fcpid：将第一个子进程id设为所有子进程的进程组以及设置前台
    //pipefd：管道描述符，每个子进程各存一份
    int fcpid, cmd_idx, pipefd[PIPESIZE][2];

    //重定向输出（输入）的新（旧）文件描述符
    int new_fd_in, old_fd_in, new_fd_out, old_fd_out;

    //父进程创建管道
    for(cmd_idx = 0; cmd_idx < cmd_cnt - 1; cmd_idx++)
    {
        if(pipe(pipefd[cmd_idx]) < 0)
        {
            perror("pipe");
            exit(1);
        }
    }

    //父进程任务：创建cmd_cnt个子进程
    //子进程任务：重定向并执行命令
    for(cmd_idx = 0; cmd_idx < cmd_cnt; cmd_idx++)
    {
        int pid = fork();
        if(pid < 0)
        {
            perror("fork");
            exit(1);
        }

        //父进程创建完子进程后继续循环
        if(pid)
        {
            if(!cmd_idx) fcpid = pid;

            //设置所有子进程进入第一个进程的进程组
            setpgid(pid, fcpid);

            //把新的进程组设置到前台
            if(!cmd_idx && !is_bg) tcsetpgrp(0, fcpid);

            continue;
        }

        //子进程关闭父进程继承下来的无关管道
        for(int j = 0; j < cmd_cnt - 1; j++)
        {
            if(j == cmd_idx || j == cmd_idx - 1) continue;
            close(pipefd[j][0]);
            close(pipefd[j][1]);
        }
        if(cmd_cnt != 1)
        {
            //第一个命令的管道重定向操作
            if(cmd_idx == 0)
            {
                dup2(pipefd[cmd_idx][1], 1);
                close(pipefd[cmd_idx][0]);
            }
            //最后一个命令的管道重定向操作
            else if(cmd_idx == cmd_cnt - 1)
            {
                dup2(pipefd[cmd_idx - 1][0], 0);
                close(pipefd[cmd_idx - 1][1]);
            }
            //其他命令的管道重定向操作
            else
            {
                dup2(pipefd[cmd_idx][1], 1);
                dup2(pipefd[cmd_idx - 1][0], 0);
                close(pipefd[cmd_idx][0]);
                close(pipefd[cmd_idx - 1][1]);
            }
        }

        //子进程解析命令及重定向
        int cmdl_cnt = 0;
        char* cmdl[MAXLEN], *cmd;
        cmd = cmdvec[cmd_idx];
        //printf("%s\n", cmd);
        cmdl[cmdl_cnt++] = strtok(cmd, " ");
        while(cmdl[cmdl_cnt] = strtok(NULL, " "))
        {
            //处理重定向输入
            if(!strcmp("<", cmdl[cmdl_cnt]))
            {
                old_fd_in = dup(0);
                char* filepath = strtok(NULL, " ");
                //printf("%s\n", filepath);
                new_fd_in = open(filepath, O_RDONLY);
                if(new_fd_in < 0)
                {
                    perror("open");
                    return;
                }
                dup2(new_fd_in, 0);
                close(new_fd_in);
            }
            //处理重定向输出
            else if(!strcmp(">", cmdl[cmdl_cnt]))
            {
                old_fd_out = dup(1);
                char* filepath = strtok(NULL, " ");
                //printf("%s\n", filepath);
                new_fd_out = open(filepath,
                                           O_TRUNC | O_CREAT | O_RDWR,
                                           0644);
                if(new_fd_out < 0)
                {
                    perror("open");
                    return;
                }
                dup2(new_fd_out, 1);
                close(new_fd_out);
            }
            //遇到后台则忽略，分割命令时候已经判断后台了
            else if(!strcmp("&", cmdl[cmdl_cnt]));
            else cmdl_cnt++;
        }

        /*
        for(int i = 0; cmdl[i]; i++)
            printf("%s\n", cmdl[i]);
        */
        execvp(cmdl[0], cmdl);
        perror("execvp");
        exit(1);

    }

    //父进程关闭管道
    for(cmd_idx = 0; cmd_idx < cmd_cnt - 1; cmd_idx++)
    {
        close(pipefd[cmd_idx][0]);
        close(pipefd[cmd_idx][1]);
    }

    for(cmd_idx = 0; cmd_idx < cmd_cnt; cmd_idx++)
    {
        if(!is_bg) wait(NULL);
    }

    //前台作业完成后，把shell设置到前台
    if(!is_bg) tcsetpgrp(0, getpid());
    //if(is_bg) printf("is bg\n");
}

//根据管道切分命令,解析是否有后台参数
//return val:分割的命令条数
int split_cmd_with_pipe(char* buf, char* retvec[MAXLEN], int* is_bg)
{
    if(strstr(buf, " &")) *is_bg = 1;
    else *is_bg = 0;
    int cnt = 0;
    retvec[cnt++] = strtok(buf, "|");
    while(retvec[cnt] = strtok(NULL, "|"))
        cnt++;
    return cnt;
}

//解析命令并运行
void run_shell(char* buf)
{
    int is_bg;
    char *cmd_vec[MAXLEN];

    //按照分割命令,解析命令是否为后台命令
    //并返回以管道为分隔符分割的命令数
    int cmd_cnt = split_cmd_with_pipe(buf, cmd_vec, &is_bg);
    if(!cmd_cnt) return;

    //判断是否为cd命令
    //如果是直接执行cd命令，否则执行exec命令
    if(!strncmp("cd ", cmd_vec[0], 3))
    {
        char *cd_temp = strtok(cmd_vec[0], " ");
        cd_temp = strtok(NULL, " ");
        run_cd(cd_temp);
    }
    else run_cmd(cmd_vec, cmd_cnt, is_bg);
}

//父进程收到子进程退出命令后，回收子进程
void handler(int sig)
{
    waitpid(-1, NULL, WNOHANG);
}

int main()
{
    //先屏蔽掉SIGTTOU信号，因为当shell从后台调用tcsetpcgrp时候会收到该信号
    signal(SIGTTOU, SIG_IGN);

    //后台设置，收到SIGCHLD后等待子进程。
    signal(SIGCHLD, handler);
    char buf[MAXLEN];
    while(1)
    {
        //获取登录用户名
        struct passwd *pw = getpwuid(getuid());
        char wd[MAXLEN], hostname[MAXLEN], *pwd;

        //获取主机名
        gethostname(hostname, MAXLEN);

        //得到当前工作目录的最后一个目录
        getcwd(wd, MAXLEN);
        pwd = wd + strlen(wd);
        for(int i = strlen(wd) - 1; *pwd != '/' && pwd != wd; pwd--);
        if(*(pwd + 1) != 0) pwd++;
        printf("%s@%s %s $ ", pw -> pw_name, hostname, pwd);

        //获取命令
        char* buf_temp;
        long int buflen = MAXLEN;
        getline(&buf_temp, &buflen, stdin);

        //如果是空命令，则continue
        if(!strcmp("\n", buf_temp)) continue;
        sscanf(buf_temp, "%[^\n]", buf);
        if(!strcmp(buf, "exit"))
        {
            printf("bye~\n");
            exit(0);
        }
        run_shell(buf);
        *buf = 0;
    }
    return 0;
}