/*
<testinfo>
test_generator=(config/mercurium-ompss config/mercurium-ompss-v2)
</testinfo>
*/

namespace N
{
    struct A
    {
        void foo()
        {
            #pragma omp task
            bar();

        }
        void bar()
        {
            int var;
            #pragma omp task
            {
            }
            #pragma omp taskwait
        }
    };
}

int main()
{
    struct N::A a;

    a.foo();
    #pragma omp taskwait
}
