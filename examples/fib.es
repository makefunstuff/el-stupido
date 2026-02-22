fib(n:🔢)->🔢{n<=1?n:fib(n-1)+fib(n-2)}
main(){i:=0;🔁 i<10{printf("fib(%d) = %d\n",i,fib(i));i+=1}}
