#include "kernel/types.h"
#include "user/user.h"

int main(void){

int  p1[2],p2[2];
char buf;
pipe(p1); pipe(p2);

if (fork()==0){
close (p1[1]);
close (p2[0]);

        for (int i=0;i<10;i++){
                read(p1[0],&buf,1);
                printf("child :child got ping :%d\n",i+1);
                write(p2[1],"x",1);
                };
}
else{
close (p1[0]);
close (p2[1]);
        for (int i=0;i<10;i++){
                write(p1[1],"x",1);
                read(p2[0],&buf,1);
                printf("parent:Parent got ping:%d\n",i+1);
                }
wait(0);
};
exit(0);
}
