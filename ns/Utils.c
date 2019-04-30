#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <curl/curl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sstream>
#include <fcntl.h>
#include "Cns_api.h"

#define FILEMETANU 128
using namespace std;

const char *volumn[]={"ino","mtime","ctime","atime","nlink","uid","dev","gid","path","size","mode"};

//read key from configure file
int get_conf_value(char *file_path, char *key_name, char *key_value)
{
        FILE *fp = NULL;
        char *line = NULL, *substr = NULL;
	char value[100];
        size_t len = 0, tlen = 0;
        ssize_t read = 0;

    if(file_path == NULL || key_name == NULL || key_value == NULL)
    {
        printf( "config_key parameter is wrong\n");
        return -1;
    }
        fp = fopen(file_path, "r");
        if (fp == NULL)
    {
        printf("open config_file failed\n");
        return -1;
    }
 while ((read = getline(&line, &len, fp)) != -1)
    {
        substr = strstr(line, key_name);
        if(substr == NULL)
        {
            continue;
        }
        else
        {
            tlen = strlen(key_name);
            if(line[tlen] == '=')
            {
                strncpy(value, &line[tlen+1], len-tlen+1);
                tlen = strlen(value);
                *(value+tlen-1) = '\0';
                break;
            }
            else
            {
                printf("config file format is invaild\n");
                fclose(fp);
                return -2;
            }
        }
    }
    if(substr == NULL)
    {
        printf("key: %s is not in config file!\n", key_name);
        fclose(fp);
        return -1;
    }
    strcpy(key_value, value);
    free(line);
    fclose(fp);
    return 0;

}

/*split path and name*/
int splitname(char *path, char *basename)
{
        char *p;

        if (*path == 0)  {
                return (-1);
        }

        if (*path != '/') {
                return (-1);
        }

        /* silently remove trailing slashes */

        p = path + strlen (path) - 1;
        while (*p == '/' && p != path)
                *p = '\0';

        if ((p = strrchr (path, '/')) == NULL)
                p = path - 1;
        strcpy (basename, (*(p + 1)) ? p + 1 : "/");
        if (p <= path)  /* path in the form abc or /abc */
                p++;
        *p = '\0';
        return (0);
}

/*remove mount path from the path*/
int pathsplit(char *path, char *mountpath)
{
	if(*path==0 || *path!='/' || *mountpath==0 || *mountpath!='/')
	{
		return -1;
	}
	char *tmp=(char *)malloc(strlen(path)-strlen(mountpath)+1);
	strcpy(tmp, path+strlen(mountpath));
	memset(path, '0', strlen(path));
	strcpy(path,tmp);
	free(tmp);
	return 0;
}

/*print json for test*/
void json_print_array(json_object *obj) 
{
      if(!obj) return;
      int length=json_object_array_length(obj);
      int i;
      for( i=0;i<length;i++) {
              json_object *val=json_object_array_get_idx(obj,i);
              json_print_value(val);
      }
}
void json_print_object(json_object *obj)
{
      if(!obj){
          printf("over\n");
          return;
          }
      json_object_object_foreach(obj,key,val) {
          printf("%s => ",key);
          json_print_value(val);
      }
}
void json_print_value(json_object *obj)
{
        if(!obj)
                return ;
        json_type type=json_object_get_type(obj);
        if(type==json_type_boolean){
                if(json_object_get_boolean(obj)) {
                 printf("true\n");
                 } else {
                        printf("false\n");
                }
        }
        else if(type == json_type_double) {
          printf("double    %lf",json_object_get_double(obj));
      } else if(type == json_type_int) {
          printf("int     %d",json_object_get_int(obj));
      } else if(type == json_type_string) {
          printf("string: %s",json_object_get_string(obj));
     } else if(type == json_type_object) {
          printf("object\n");
          json_print_object(obj);
      } else if(type == json_type_array) {
          json_print_array(obj);
          printf("array\n");
      } else {
          printf("ERROR\n");
      }
      printf("\n");
}

/*mdir multi-level dir*/
int mkdirs(char *muldir, int mode)
{
	int res;
	int i,len;
	char str[512];
	strncpy(str, muldir, 512);
	len=strlen(str);
	for(i=0; i<len; i++){
		if(str[i]=='/' && i!=0){
			str[i]='\0';
			if(access(str, 0)!=0){
				if((res=mkdir(str,mode))!=0)
					return res;
			}
			str[i]='/';
		}
	}
	if(len>0 &&access(str, 0)!=0){
		if((res=mkdir(str, mode))!=0)
			return res;
	}
	return 0;
}

int SplitString(string s, vector <string>& v)
{
	string result;
        stringstream input(s);
        while(input>>result)
                v.push_back(result);
	return 0;
}

static size_t process_data(char *buffer, size_t size, size_t nmemb, void *user_p)
{
	FILE *fp = (FILE *)user_p;
	size_t return_size = fwrite(buffer, size, nmemb, fp);
	return return_size;
}

int get_jsonbycurl(char *url_path, char *cachefile)
{
	int res=0;
	CURL *curl;
	CURLcode ret;
	FILE *fp=fopen(cachefile, "ab+");
	curl_global_init(CURL_GLOBAL_ALL);
	curl=curl_easy_init();
	if(curl){
		curl_easy_setopt(curl, CURLOPT_URL, url_path);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &process_data);
        	curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
        	ret=curl_easy_perform(curl);
		if(ret!=CURLE_OK ){
			fprintf(stderr, "curl_easy_perform() failed: %s\n",curl_easy_strerror(ret));
            		remove(cachefile);
            		res=1;
        	}
        	curl_global_cleanup();
	}else
		res=1;
	fclose(fp);
	return res;
}

int get_josnobjumber(json_object *obj){
	int res=0;
        if(!obj)
                return -1;
        json_object_object_foreach(obj,key,val){
                json_type type=json_object_get_type(obj);
                if(type==json_type_object){
                        res++;
                 }else{
                        fprintf(stderr,"Json_object get type error\n");
                        return -1;
                }
        }
        return res;
}

void printf_cnsfilestat(int t, struct Cns_filestat st){
        printf("num:%d  name:%s  ino:%ld  mtime:%d  ctime:%d  atime:%d  nlink:%d  uid:%d  dev:%d  gid:%d  path:%s  size:%ld  mode:%d\n",t,st.name,st.ino,st.mtime,st.ctime,st.atime,st.nlink,st.uid,st.dev,st.gid,st.path,st.filesize,st.filemode);
}


int match_column(const char *vol){
        int i;
        for(i=0; i<11; i++){
                if(strcmp(vol,volumn[i])==0){
                        return i;
                }
        }
        return -1;
}

int set_columnvalue(json_object *obj, struct Cns_filestat &st){
        int i=0;
	string column_name[11];
	string column[11];
        if(!obj)
                return 1;
        json_object_object_foreach(obj,key,val){
                json_type type=json_object_get_type(obj);
                if(type==json_type_object){
                        column_name[i]=key;
			column[i]=json_object_get_string(val);
                        i++;
                }else{
                        printf("Json_object is wrong\n");
                }
        }

        for(int j=0; j<11; j++){
                int location=match_column(column_name[j].c_str());
                if(location==-1)
                        fprintf(stderr,"NO such a column %s\n",column_name[j].c_str());
                else{
                        switch(location){
                                case 0:st.ino=atol(column[j].c_str());
					break;
                                case 1:st.mtime=atol(column[j].c_str());
					break;
                                case 2:st.ctime=atol(column[j].c_str());
					break;
                                case 3:st.atime=atol(column[j].c_str());
					break;
                                case 4:st.nlink=atol(column[j].c_str());
					break;
                                case 5:st.uid=atoi(column[j].c_str());
					break;
                                case 6:st.dev=atol(column[j].c_str());
					break;
                                case 7:st.gid=atoi(column[j].c_str());
					break;
                                case 8:strcpy(st.path,column[j].c_str());
					break;
                                case 9:st.filesize=atol(column[j].c_str());
					break;
                                case 10:st.filemode=atoi(column[j].c_str());
					break;
                        }
                }
        }
        return 0;
}

int get_metadatabyjson(char *cachefile, vector <string> &filename, vector <struct Cns_filestat> &st)
{
	int t, t2;
	int res;
	int filenum=0;
	struct Cns_filestat st_tmp;
	FILE *fp=fopen(cachefile, "r");
        if(fp==NULL){
                fprintf(stderr,"cannot get metada from cachefile\n");
                return 1;
        }
	int jsonlen=512*FILEMETANU;
        fseek(fp,0,SEEK_SET);	
        char *buf=(char *)malloc(jsonlen*sizeof(char)+1);
	t=0;
        while(fgets(buf,jsonlen,fp)!=NULL){		
	
        json_object *obj=json_tokener_parse(buf);
        if(is_error(obj)){
                fprintf(stderr, "Transfer Error Usag:\n%s\n", buf);
                remove(cachefile);
                return 2;
        }
        int i=0;
        if(!obj){
                fprintf(stderr,"get no json_object!\n");
                return 1;
        }
	t2=0;
        json_object_object_foreach(obj,key,val){
                if(!obj){
                        fprintf(stderr,"get no json_object!\n");
                        return 1;
                }
                json_type type=json_object_get_type(obj);
                if(type==json_type_object){
			if(t!= 0 && t2 == 0){
			
			}else{
                        filename.push_back(key);
                        if(set_columnvalue(val, st_tmp))
                                fprintf(stderr, "metadata of file %s is error\n", filename[i].c_str());
			strcpy(st_tmp.name, filename[i].c_str());
			st.push_back(st_tmp);
                        i++;
			}
                }else{
                       fprintf(stderr, "%s it is not json_object\n", key);
                }
		t2++;
        }
	memset(buf, '\0', strlen(buf));
	json_object_put(obj);
	t++;
	}
	free(buf);
        fclose(fp);
        remove(cachefile);
        return 0;
}

int touchvf(char *path, int filesize, int flag)
{
        int fd;
        off_t offset;
        char path_tmp[100];
        strcpy(path_tmp, path);

        fd=open(path_tmp,O_RDWR|O_CREAT,S_IRUSR|S_IRGRP|S_IROTH);
        if(-1 == fd)
        {
            perror("creat");
            return 1;
        }
        if(flag==0){
                offset = lseek(fd, filesize-1, SEEK_END);
                write(fd, "", 1);
        }
        close(fd);
        return 0;

}


int create_vpath(char *mdir, char *path, int mode){
        char mountpath[128];
        int res;
        sprintf(mountpath, "%s%s", mdir, path);
        if(access(mountpath,0)){
                if(S_ISDIR(mode)){
                        if(mkdirs(mountpath, 0777)!=0){
                                fprintf(stderr, "mountdir create fail\n");
                                return 1;
                        }
                }else{
                        if(touchvf(mountpath, 0, 0)){
                                fprintf(stderr, "virfile: %s create failure\n", mountpath);
                                return 1;
                        }
                }
        }
        return 0;
}

unsigned int RSHash(char *str)
{
    unsigned int b = 378551;
    unsigned int a = 63689;
    unsigned int hash = 0;

    while (*str)
    {
        hash = hash * a + (*str++);
        a *= b;
    }

    return (hash & 0x7FFFFFFF);
}

