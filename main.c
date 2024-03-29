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
  float xincrement;
  int total;
  int from;
  int to;
};

struct MetadataCtx{
  struct ChannelData * ch;
  int step;
  int lastCh;
  int chIdx;
};

void cmd(int fd, const char * s, ...){
  char buffer[100];
  va_list args;
  va_start(args, s);
  int len = vsprintf(buffer, s, args);
  va_end(args);
  write(fd, buffer, len); 
}

zip_int64_t metadata_cb(void *userdata, void *data, zip_uint64_t len, zip_source_cmd_t command){
  struct MetadataCtx * ctx = userdata;
  if (command == ZIP_SOURCE_SUPPORTS){
    return  zip_source_make_command_bitmap(ZIP_SOURCE_OPEN, ZIP_SOURCE_READ, ZIP_SOURCE_CLOSE, ZIP_SOURCE_STAT, ZIP_SOURCE_ERROR);
  }
  if (command == ZIP_SOURCE_OPEN){
    ctx->step = 0;
    ctx->lastCh = 0;
    ctx->chIdx = 0;
  }
  if (command == ZIP_SOURCE_READ){
    if (ctx->step == 0){
      int channels = 0;
      float xinc;
      for (int i = 0; i < 4; i++){
        if (ctx->ch[i].fd){
          channels++;
          xinc = ctx->ch[i].xincrement;
        }
      }
      ctx->step = 1;
      return sprintf(data, "[device 1]\nsamplerate=%d Hz\ntotal analog=%d\n", (int)(1 / xinc), channels);
    }
    if (ctx->step == 1){
      printf("channel metadata\n");
      for (int i = ctx->lastCh; i < 4; i++){
        printf("checking %d\n", i);
        if (ctx->ch[i].fd){
          ctx->lastCh = i + 1;
          ctx->chIdx ++;
          printf("writing to %lu\n", len);
          return sprintf(data, "analog%d=CH%d\n", ctx->chIdx, i + 1);
        }
      }
    }
  }
  return 0;
}

zip_int64_t zip_cb(void *userdata, void *data, zip_uint64_t len, zip_source_cmd_t command){
  struct ChannelData * ctx = userdata;
  //printf("CB CALLED %d %d\n", command, ctx->idx);
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
      int count;
      float xorigin;
      float xreference;
      printf("read %s\n", buff);
      sscanf(buff, "%d,%d,%d,%d,%f,%f,%f,%f,%f,%f", &format, &type, &(ctx->total), &count, &(ctx->xincrement), &xorigin, &xreference, &(ctx->yincrement), &(ctx->yorigin), &(ctx->yreference));
      printf("parsed %d,%d,%d,%d,%f,%f,%f,%f,%f,%f\n", format, type, ctx->total, count, ctx->xincrement, xorigin, xreference, ctx->yincrement, ctx->yorigin, ctx->yreference);
      ctx->from = 0;
      ctx->to = 0;
  }
  if (command == ZIP_SOURCE_READ){
      if (ctx->total == 0){
        return 0;
      }
      char buff[50];
      int size;
      if (ctx->from == ctx->to){
        size = 10000;
        if (size > ctx->total){
          size = ctx->total;
        }
        ctx->to = ctx->from + size;
        cmd(ctx->fd,":WAV:START %d", ctx->from + 1); //indexed from 1 and including last position
        cmd(ctx->fd,":WAV:STOP %d", ctx->to);
        cmd(ctx->fd,":WAV:DATA?"); 
        int size = read(ctx->fd, buff, 11);
        buff[size] = 0;
        int sum;
        sscanf(buff, "#9%d", &sum);
        printf("readed %d, %s, %d, %d, %d\n",size,  buff, sum, ctx->from, ctx->to); 
        //TODO check sum should be = form
      }
      int readlen = 50;
      if (readlen * 4 > len){
        readlen = len / 4;
      }
      if (readlen > (ctx->to - ctx->from)){
        readlen = ctx->to - ctx->from;
      }
      ctx->from += readlen;
      ctx->total -= readlen;
      usleep(10);
      size = read(ctx->fd, buff, readlen);
      if (size < 0){
        printf("seconf attempt\n");
        size = read(ctx->fd, buff, readlen);
        if (size >= 0){
          printf("seconf succesfull\n");
        }
      }
      if (size < 0){
        printf("end of file\n");  //TODO number of bytes is known so it does not need to try download last data
        return 0;
      }
      float *d = data;
      for( int i =0; i < size; i++){ //TODO get rid of the last byte
        d[i] = (unsigned char) buff[i];
        d[i] -= ctx->yreference;
        d[i] -= ctx->yorigin;
        d[i] *= ctx->yincrement;
        //printf("readed %f from %d", d[i], ctx->idx);
      }
      //printf("returning %d\n", size * 4);
      return size * 4;
  }
  return 0;
};


int main(int argc, char * argv[]){
   if (argc < 3){
      printf("usage %s /dev/usbtmc output.sr\n", argv[0]);
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

  zip_t *zip = zip_open(argv[2], ZIP_CREATE | ZIP_TRUNCATE, NULL);
  struct ChannelData channels[4];
  int outChNum = 0;
  for (int i=0; i < 4; i++){
    cmd(fd,":CHAN%d:DISP?", i + 1); 
    size = read(fd, buff, 99);
    if (size < 1){
      return 1;
    }
    if (buff[0] == '1'){
      printf("channel %d enabled\n", i + 1);
      char filename[20];
      sprintf(filename, "analog-1-%d-1", ++outChNum);
      channels[i].idx = i;
      channels[i].fd = fd;
      struct zip_source *source = zip_source_function(zip, &zip_cb, &channels[i]);
      zip_file_add(zip, filename, source, ZIP_FL_OVERWRITE);
    }else{
      channels[i].fd = 0;
    }
  }
  struct zip_source *source = zip_source_buffer(zip, "2", 1, 0);
  zip_file_add(zip, "version", source, ZIP_FL_OVERWRITE);

  struct MetadataCtx ctx;
  ctx.ch = channels;
  source = zip_source_function(zip, &metadata_cb, &ctx);
  zip_file_add(zip, "metadata", source, ZIP_FL_OVERWRITE);

  printf("closing\n");
  zip_close(zip);
  printf("closed\n");
  close(fd);
  return 0;
}
