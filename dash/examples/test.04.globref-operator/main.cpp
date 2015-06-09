/* 
 * globref/main.cpp 
 *
 * author(s): Karl Fuerlinger, LMU Munich */
/* @DASH_HEADER@ */

#include <unistd.h>
#include <iostream>
#include <libdash.h>

#define SIZE 10

class Foo {
public:
  double operator[](int pos) {
    return 33.3;
  }
};

class Bar {
};

using namespace std;

int main(int argc, char* argv[])
{
  dash::init(&argc, &argv);
  
  auto myid = dash::myid();
  auto size = dash::size();

  dash::Array<Foo> arr(SIZE);
  
  if(myid==0) {
    dash::GlobRef<Foo> r1 = arr[0];
#if 0
    cout << r1[33] << endl;
#endif
  }
  
  arr.barrier();

  if(myid==size-1) {

  }
  
  dash::finalize();
}