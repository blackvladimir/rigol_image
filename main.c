#include<fcntl.h>
#include<unistd.h>
#include<stdio.h>
#include<string.h>
#include<stdarg.h>

void cmd(int fd, const char * s, ...){
  char buffer[100];
  va_list args;
  va_start(args, s);
  int len = vsprintf(buffer, s, args);
  va_end(args);
  buffer[len++] = '\n';
  write(fd, buffer, len); 
}

int main(int argc, char * argv[]){
   if (argc < 2){
      printf("usage %s /dev/usbtmc\n", argv[0]);
      return 1;
   }
  int fd = open(argv[1], O_RDWR);
  if (fd < 0){
    printf("can not open %s\n", argv[1]);
    return 1;
  }
  write(fd,"*IDN?\n", 6); 
  char buff[100];
  int size = read(fd, buff, 99);
  buff[size] = 0;
  printf("model %s\n", buff);

  for (int i=0; i < 4; i++){
    cmd(fd,":CHAN%d:DISP?", i + 1); 
    size = read(fd, buff, 99);
    if (size < 1){
      return 1;
    }
    if (buff[0] == '1'){
      printf("channel %d enabled\n", i + 1);
      char filename[20];
      sprintf(filename, "analog-1-%d-1", i + 1);
      int fout = open(filename, O_WRONLY | O_CREAT, 0660);
      if (fout < 0){
        printf("can not open %s\n", filename);
        return 1;
      }

      cmd(fd,":WAV:SOUR CHAN%d", i + 1); 
      cmd(fd,":WAV:MODE RAW"); 
      cmd(fd,":WAV:FORM BYTE"); 
      cmd(fd,":WAV:PRE?"); 
      size = read(fd, buff, 50);
      if (size == 50){
        size += read(fd, buff + size, 50);  //can not be read more than 52 at a time
      }
      buff[size] = 0;
      printf("readed %d, %s\n",size,  buff);
      int format;
      int type;
      int points;
      int count;
      float xincrement;
      float xorigin;
      float xreference;
      float yincrement;
      float yorigin;
      float yreference;
      sscanf(buff, "%d,%d,%d,%d,%f,%f,%f,%f,%f,%f", &format, &type, &points, &count, &xincrement, &xorigin, &xreference, &yincrement, &yorigin, &yreference);
      printf("parsed %d,%d,%d,%d,%f,%f,%f,%f,%f,%f\n", format, type, points, count, xincrement, xorigin, xreference, yincrement, yorigin, yreference);
      cmd(fd,":WAV:START 1");  //TODO read in multpiple passes
      cmd(fd,":WAV:STOP 6000");  //TODO read in multpiple passes
      cmd(fd,":WAV:DATA?"); 
      size = read(fd, buff, 11);
      buff[size] = 0;
      int sum;
      sscanf(buff, "#9%d", &sum);
      printf("readed %d, %s, %d\n",size,  buff, sum); 
      do{
        size = read(fd, buff, 50);
        for( int i =0; i < size && sum > 0; i++, sum--){
          float val = (unsigned char) buff[i];
          val -= yreference;
          val -= yorigin;
          val *= yincrement;
          write(fout, &val, 4); 
          //printf("%d, %f\n", (unsigned char) buff[i], val);
        }
      }while(size == 50);
      printf("SUM %d %s %d\n", sum, filename, fout);
      close(fout);
    }
  }
  close(fd);
  return 0;
}
