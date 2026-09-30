
#include <stdio.h>
#include <stdlib.h>

int simple_if(int x)
{
    if (x > 0) {
        return x + 1;
    }
    return 0;
}

int nested_if(int x)
{
    int r = 0;

    if (x > 100) {
        if (x > 1000) {
            r = 3;
        } else {
            r = 2;
        }
    } else {
        if (x > 0) {
            r = 1;
        }
    }
    return r;
}

int loop_sum(int n)
{
    int sum = 0;
    int i;

    for (i = 0; i < n; i++) {
        sum += i;
    }

    while (sum > 100) {
        sum -= 10;
    }

    return sum;
}

int classify(int x)
{
    int sum = 0;
    int i;

    if (x > 100) {
        sum = 100;
    } else if (x > 0) {
        sum = x;
    } else {
        sum = -x;
    }

    for (i = 0; i < 3; i++) {
        sum = sum + i;
    }

    return sum;
}

int switch_test(int x)
{
    switch (x) {
    case 0:  return 10;
    case 1:  return 20;
    case 2:  return 30;
    case 3:  return 40;
    case 4:  return 50;
    default: return -1;
    }
}

int call_test(int x)
{
    int a = simple_if(x);
    int b = nested_if(x);
    return a + b + loop_sum(x);
}

int main(int argc, char **argv)
{
    int v = 42;

    if (argc > 1) {
        v = atoi(argv[1]);
    }

    printf("simple_if(%d)   = %d\n", v, simple_if(v));
    printf("nested_if(%d)   = %d\n", v, nested_if(v));
    printf("loop_sum(%d)    = %d\n", v, loop_sum(v));
    printf("classify(%d)    = %d\n", v, classify(v));
    printf("switch_test(%d) = %d\n", v, switch_test(v));
    printf("call_test(%d)   = %d\n", v, call_test(v));
    return 0;
}
