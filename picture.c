#include<fcntl.h>
#include<unistd.h>
#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<stdarg.h>
#include<errno.h>

void cmd(int fd, const char * s, ...){
  char buffer[100];
  va_list args;
  va_start(args, s);
  int len = vsprintf(buffer, s, args);
  va_end(args);
  write(fd, buffer, len); 
}

int main(int argc, char * argv[]){
   if (argc < 3){
      printf("usage %s /dev/usbtmc output.png\n", argv[0]);
      return 1;
   }
  int fd = open(argv[1], O_RDWR);
  if (fd < 0){
    printf("can not open %s\n", argv[1]);
    return 1;
  }
  char buff[200];
  int out = open(argv[2], O_CREAT | O_WRONLY, S_IRUSR | S_IRGRP | S_IWUSR);
  write(fd,"*IDN?\n", 6); 
  int size = read(fd, buff, 99);
  buff[size] = 0;
  printf("model %s\n", buff);

  cmd(fd,":DISP:DATA? ON,OFF,PNG"); 
  size = read(fd, buff, 2);
  printf("size %d\n", size);
  if (buff[0] != '#'){
    printf("unknown data start\n");
    return 1;
  }
  int digits = buff[1] - '0';
  size = read(fd, buff, digits);
  buff[size] = 0;
  printf("size %d\n", size);
  printf("length %s\n", buff);
  int len = atoi(buff);
  printf("length %d\n", len);
  while (len > 0){
    usleep(1000);
    size = read(fd, buff, 50);
    printf("readed %d remaining %d\n", size, len);
    write(out, buff, size);
    len -= size;
  }
  close(fd);
  close(out);
  return 0;
}
