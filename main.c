#include<fcntl.h>
#include<unistd.h>
#include<stdio.h>
#include<string.h>
#include<stdarg.h>
#include<zip.h>

struct ChannelData{
  int idx;
  int fd;
  float yreference;
  float yorigin;
  float yincrement;
};

void cmd(int fd, const char * s, ...){
  char buffer[100];
  va_list args;
  va_start(args, s);
  int len = vsprintf(buffer, s, args);
  va_end(args);
  buffer[len++] = '\n';
  write(fd, buffer, len); 
}

zip_int64_t zip_cb(void *userdata, void *data, zip_uint64_t len, zip_source_cmd_t command){
  struct ChannelData * ctx = userdata;
  printf("CB CALLED %d %d\n", command, ctx->idx);
  if (command == ZIP_SOURCE_SUPPORTS){
    return  zip_source_make_command_bitmap(ZIP_SOURCE_OPEN, ZIP_SOURCE_READ, ZIP_SOURCE_CLOSE, ZIP_SOURCE_STAT, ZIP_SOURCE_ERROR);
  }
  if (command == ZIP_SOURCE_OPEN){
      cmd(ctx->fd,":WAV:SOUR CHAN%d", ctx->idx + 1); 
      cmd(ctx->fd,":WAV:MODE RAW"); 
      cmd(ctx->fd,":WAV:FORM BYTE"); 
      cmd(ctx->fd,":WAV:PRE?"); 
      char buff[100];
      int size = read(ctx->fd, buff, 50);
      if (size == 50){
        size += read(ctx->fd, buff + size, 50);  //can not be read more than 52 at a time
      }
      buff[size] = 0;
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
      sscanf(buff, "%d,%d,%d,%d,%f,%f,%f,%f,%f,%f", &format, &type, &points, &count, &xincrement, &xorigin, &xreference, &(ctx->yincrement), &(ctx->yorigin), &(ctx->yreference));
      printf("parsed %d,%d,%d,%d,%f,%f,%f,%f,%f,%f\n", format, type, points, count, xincrement, xorigin, xreference, yincrement, yorigin, yreference);
      cmd(ctx->fd,":WAV:START 1");  //TODO read in multpiple passes
      cmd(ctx->fd,":WAV:STOP 6000");  //TODO read in multpiple passes
      cmd(ctx->fd,":WAV:DATA?"); 
      size = read(ctx->fd, buff, 11);
      buff[size] = 0;
      int sum;
      sscanf(buff, "#9%d", &sum);
      printf("readed %d, %s, %d\n",size,  buff, sum); 
  }
  if (command == ZIP_SOURCE_READ){
      char buff[50];
      int size = 50;
      if (size * 4 > len){
        size = len / 4;
      }
      size = read(ctx->fd, buff, size);
      if (size < 0){
        printf("end of file\n");  //TODO number of bytes is known so it does not need to try download last data
        return 0;
      }
      float *d = data;
      printf("len %lu\n", len);
      for( int i =0; i < size; i++){ //TODO get rid of the last byte
        d[i] = (unsigned char) buff[i];
        d[i] -= ctx->yreference;
        d[i] -= ctx->yorigin;
        d[i] *= ctx->yincrement;
      }
      printf("returning %d\n", size * 4);
      return size * 4;
  }
  return 0;
};


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

  zip_t *zip = zip_open("/tmp/pok.sr", ZIP_CREATE | ZIP_TRUNCATE, NULL);
  struct ChannelData channels[4];
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
      channels[i].idx = i;
      channels[i].fd = fd;
      struct zip_source *source = zip_source_function(zip, &zip_cb, &channels[i]);
      zip_file_add(zip, filename, source, ZIP_FL_OVERWRITE);
    }
  }
  struct zip_source *source = zip_source_buffer(zip, "2", 1, 0);
  zip_file_add(zip, "version", source, ZIP_FL_OVERWRITE);
  source = zip_source_buffer(zip, "[device 1]\nsamplerate=500000000 Hz\ntotal analog=2\nanalog1=CH1\nanalog2=CH2", 72, 0); //TODO add channels and samplerate automaticaly
  zip_file_add(zip, "metadata", source, ZIP_FL_OVERWRITE);

  printf("closing\n");
  zip_close(zip);
  printf("closed\n");
  close(fd);
  return 0;
}
