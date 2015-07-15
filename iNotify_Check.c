/* Compilacion: 
                 gcc iNotify_Check.c -o iNotify_Check
 
   Ejecucion:
                 ./iNotify_Check  /opt/RUTA_PRUEBA
 
*/
#include "libraries_include.h"

#define EVENT_MASK (IN_CLOSE_WRITE|IN_CREATE|IN_DELETE|IN_DELETE_SELF|IN_MODIFY|IN_MOVE_SELF\
    |IN_MOVED_FROM|IN_MOVED_TO|IN_DONT_FOLLOW|IN_ONLYDIR|IN_ATTRIB)
    
        //Variables para Socket
    int sock = 0, sock_send = 0;
    char sendBuff[1025];    

typedef struct {
    int wd;
    int parent_wd;
    char *name;
} _watchstruct;

char events_buf[PATH_MAX + sizeof(struct inotify_event) + 1];
static _watchstruct *watches;
int ifd = 0, max_watches;
char *watch_dir;

static void wd_path(int wd, char *path)
{
     if (wd == 0) {
        strcpy(path, watch_dir);
        strcat(path, "/");
        return;
    }

    if (wd < 0 || !watches[wd].name) {
        printf("Recursivo %d, %x\n", wd, watches[wd].name);
        printf("Corrupcion de Memoria..\n");
        exit(1);
    }
    
    wd_path(watches[wd].parent_wd, path);
    if (watches[wd].name[0] == 0) {
      return;
    }

    strcat(path, watches[wd].name);
    strcat(path, "/");
}

static int add_dir_watch(int parent_wd, char *dir, char *dir_name, int no_print)
{
    int wd = inotify_add_watch(ifd, dir, EVENT_MASK);
    
    if (wd < 0) {
        printf("NO se puede agregar a watch: '");
        printf(dir);
        printf("' Usando inotify: ");
        if (errno == ENOSPC) {
            printf("Muchas entradas\nNecesitas incrementar el numero de Monioreo en: /proc/sys/fs/inotify/max_user_watches");
        } else {
	    printf("->");
            printf(strerror(errno));
        }
        printf("\n");	
        if (errno != EACCES && errno != ENOENT) exit(1);
        return wd;
    }

    if (wd >= max_watches) {
        printf("\nMuchos Eventos; Necesitas reiniciar por OverFlow de File Descriptors.\n");
        exit(3);
    }

    dir_name = strdup(dir_name);
    if (!dir_name) {
        printf("Cannot strdup(dir_name)\n");
        exit(1);
    }

    watches[wd].wd = wd;
    watches[wd].parent_wd = parent_wd;
    
    if (watches[wd].name) {
      free(watches[wd].name);
    }
    
    watches[wd].name = dir_name;
    return wd;
}

static void add_dir(int dir_wd, char *dir, int errors_fatal, int no_print)
{
    char path[PATH_MAX + 1];
    DIR *dh = opendir(dir);
    struct dirent *ent;
    struct stat st;
    int dirl = strlen(dir), n = sizeof(path) - 1 - dirl, had_errors = 0, wd;
    
    if (dirl > sizeof(path) - 3) {
        printf("Ruta muy larga (not watched): ");
        printf(dir);
        printf("\n");

        if (errors_fatal) {
	  exit(1);
	}
        return;
    }

    if (!dh) {
        printf("No se puede leer(");
        printf(dir);
        printf("): ");
        printf(strerror(errno));
        printf("\n");

        if (errors_fatal) exit(1);
        return;
    }
    
    strcpy(path, dir);
    while ((ent = readdir(dh)) != NULL) {
        if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) {
	  continue;
	}
        path[dirl] = '/';
        path[dirl + 1] = 0;
        strncat(path + dirl, ent->d_name, n);
        path[sizeof(path) - 1] = 0;
	
        if (lstat(path, &st)) {
            printf("No se puede leer(");
            printf(path);
            printf("): ");
            printf(strerror(errno));
            printf("\n");
            had_errors = 1;
            continue;
        }

        if (S_ISDIR(st.st_mode)) { 
            wd = add_dir_watch(dir_wd, path, ent->d_name, no_print);
            if (wd < 0) continue;
            add_dir(wd, path, errors_fatal, no_print);
        }
    }
    closedir(dh);

    if (errors_fatal && had_errors) {
      exit(1);
    }
}

static int do_watch(int max_watches)
{
    struct inotify_event *ev = (struct inotify_event*)events_buf;
    ssize_t n = 0, wd;
    char path[PATH_MAX + 1];
    char Cadena1[PATH_MAX + 1], Cadena2[PATH_MAX + 1];
    
 
 
    
    watches = (_watchstruct*) calloc(max_watches, sizeof(_watchstruct));
    if (!watches) {
        printf("No se puede alojar en memoria\n");
        exit(1);
    }

    ifd = inotify_init();
    if (ifd == -1) {
        perror("No se puede iniciar inotify");
        exit(1);
    }
    
    wd = add_dir_watch(0, watch_dir, "", 1);
    if (wd < 0) {
        printf("No se puede Agregar Ruta\n");
        exit(1);
    }
    add_dir(wd, watch_dir, 1, 1);
          
    while ((n = read(ifd, events_buf, sizeof(events_buf))) > 0) {
        ev = (struct inotify_event*)events_buf;
        while (n > 0) {
            if (ev->mask & IN_Q_OVERFLOW) {
                printf("OverFlow de Cola, necesita reinicio.\n");
                exit(3);
            }
            struct stat buf_stat;

            if (ev->mask & IN_IGNORED) {
                free(watches[ev->wd].name);
                watches[ev->wd].parent_wd = -1;
                watches[ev->wd].name = NULL;
                goto loop_end;
            }

            wd_path(ev->wd, path);

            if ((ev->mask & IN_DELETE) || (ev->mask & IN_MOVED_FROM)) {
	      goto loop_end;
            }

            if (ev->mask & IN_ISDIR && ev->mask & IN_CREATE) { //IF PARA DIRECTORIO
                if (ev->len + strlen(path) > sizeof(path) - 1) {
                    printf("Too deep directory: ");
                    printf(path);
                    printf(ev->name);
                    printf("\n");
                    goto loop_end;
                }
                strcat(path, ev->name);
                wd = add_dir_watch(ev->wd, path, ev->name, 0);
                if (wd < 0){
		  goto loop_end;
		}
                add_dir(ev->wd, path, 0, 0);
            } //TERMINA IF PARA DIRECTORIO
            
	      if (ev->mask & IN_ATTRIB){
		if (ev->mask & IN_ISDIR){
		  stat(path, &buf_stat);
		  if (buf_stat.st_mode & S_IWOTH) {
		    snprintf(sendBuff, sizeof(sendBuff),"Directorio con Escritura Publica: %s\r\n",path);
		    //sock_send = send(sock, sendBuff, strlen(sendBuff),0);
		    sock_send = write(sock, sendBuff, strlen(sendBuff));
		    if( n < 0 ){
		      printf("Error al enviar a Socket\n");
		    }
		    printf("Directorio con Escritura Publica: %s\n",path);
		  }
		}
		else{
		  strcpy(Cadena1, path);
		  strcpy(Cadena2, ev->name);
		  strcat(Cadena1, Cadena2);
		  stat(Cadena1, &buf_stat);
		  if (buf_stat.st_mode & S_IWOTH) {
		    //sock = OS_ConnectPort(514,"192.168.221.128");
		    //if( sock <= 0 ){
		      //printf("Error conexion al Socket con Archivos\n");
		      //exit(1);
		    //}
		    snprintf(sendBuff, sizeof(sendBuff),"Archivo con Escritura Publica: %s\r\n",Cadena1);
		    //sock_send = send(sock, sendBuff, strlen(sendBuff),0);
		    sock_send = write(sock, sendBuff, strlen(sendBuff));
		    if( n < 0 ){
		      printf("Error al enviar a Socket\n");
		    }
		    printf("Archivo con Escritura Publica: %s\n",Cadena1);
		  }
		  goto loop_end;
		}
	      }
            loop_end:
            n -= sizeof(struct inotify_event) + ev->len;
            ev = (struct inotify_event*) ((char*)ev + sizeof(struct inotify_event) + ev->len);
        }//Fin de Segundo While
    }//Fin de Primer While
    OS_CloseSocket(sock);
    perror("No se puede leer() inotify queue");
    exit(1);
}

int main(int argc, char *argv[])
{
    int fd, n;
    char buf[12];

    pid_t pid = getpid();
    FILE *fpid = fopen("./iNotify_Agent.pid", "w");
    if (!fpid){
      perror("Archivo PID Error\n");
      exit(EXIT_FAILURE);
    }
    fprintf(fpid, "%d\n", pid);
    fclose(fpid);
    
    if (argc != 2) {
        printf("Usage: notify <dir>\n");
        return 1;
    }

    fd = open("/proc/sys/fs/inotify/max_user_watches", O_RDONLY);
    if (fd < 0) {
        perror("No se puede abrir /proc/sys/fs/inotify/max_user_watches");
        return 1;
    }

    if ( (n = read(fd, buf, sizeof(buf) - 1)) < 0) {
        perror("No se puede leer() /proc/sys/fs/inotify/max_user_watches");
        return 1;
    }

    buf[n] = 0;
    max_watches = atoi(buf) * 2;
    if (max_watches <= 0) {
        printf("Numero de Rutas Incorrecto: ");
        printf(buf);
        printf("\n");
        return 1;
    }
    
    sock = OS_ConnectPort(514,"192.168.221.128");
    if( sock <= 0 ){
      printf("Error conexion al Socket\n");
      exit(1);
    }
    
    watch_dir = argv[1];
    do_watch(max_watches);
    return 0;
}
