#include <cstdlib>
#include <iostream>
#include <cassert>
using namespace std;
#include "dco.hpp"
using namespace dco;
typedef gt1s<double>::type DCO_BASE_TYPE;
typedef ga1s<DCO_BASE_TYPE> DCO_MODE;
typedef DCO_MODE::type DCO_TYPE;
typedef DCO_MODE::tape_t DCO_TAPE_TYPE;
typedef DCO_TAPE_TYPE::iterator_t DCO_TAPE_POSITION_TYPE;

#include "../include/f.hpp"

template<typename DCO_MODE>
void LUDecomp_fill_gap(typename DCO_MODE::external_adjoint_object_t *D) {
  (void) D;
}

template<typename DCO_MODE, typename TYPE>
void LUDecomp_make_gap(vector<TYPE>& A){
  typedef typename DCO_MODE::external_adjoint_object_t DCO_EAO_TYPE;

  DCO_EAO_TYPE *D = DCO_MODE::global_tape->template create_callback_object<DCO_EAO_TYPE>();

  size_t n=sqrt(double(A.size()));
  vector<DCO_BASE_TYPE> Ap(n*n);
  for(size_t i=0;i<n*n;i++) Ap[i]=value(A[i]);
  LUDecomp(Ap);
  for(size_t i=0;i<n;i++)	
    for(size_t j=0;j<n;j++) 
      value(A[i*n+j])=Ap[i*n+j];
			
  DCO_MODE::global_tape->insert_callback(LUDecomp_fill_gap<DCO_MODE>,D);
}

// U^T*y=b
template <class TYPE>
inline void FSubstT(const vector<TYPE>& LU, vector<TYPE>& y) {
  size_t n=y.size();
  for (size_t i=0;i<n;i++){
    for (size_t j=0;j<i;j++) 
      y[i]=y[i]-LU[j*n+i]*y[j];
    y[i]=y[i]/LU[i*n+i];
  }
}

// L^T*x=y
template <class TYPE>
inline void BSubstT(const vector<TYPE>& LU, vector<TYPE>& b) {
  size_t n=b.size();
  for (size_t k=n,i=n-1;k>0;k--,i--) 
    for (size_t j=n-1;j>i;j--) 
      b[i]=b[i]-LU[j*n+i]*b[j];
}

// LU^T*x=y
template <class TYPE>
inline void SolveT(const vector<TYPE>& LU, vector<TYPE>& b) {
  FSubstT(LU,b);
  BSubstT(LU,b);
}

template<typename DCO_MODE>
void Solve_fill_gap(typename DCO_MODE::external_adjoint_object_t *D) {
  typedef typename DCO_MODE::type DCO_TYPE;
  vector<DCO_TYPE>* LU_p = D->template read_data<vector<DCO_TYPE>*>();
  size_t n=sqrt(double(LU_p->size()));
  vector<DCO_TYPE> T(n), a1T(n);

  for (size_t i=0;i<n;i++) a1T[i]=D->get_output_adjoint();
  DCO_MODE::global_tape->switch_to_passive();
  SolveT(*LU_p,a1T); 
  for (size_t i=0;i<n;i++) T[i]=D->template read_data<DCO_BASE_TYPE>();
  for (size_t i=0;i<n;i++)
    for (size_t j=0;j<n;j++)
      D->increment_input_adjoint(value(-a1T[i]*T[j])); 
  for (size_t i=0;i<n;i++)
    D->increment_input_adjoint(value(a1T[i]));
  DCO_MODE::global_tape->switch_to_active();
}

template<typename DCO_MODE, typename TYPE>
void Solve_make_gap(const vector<TYPE>& LU, vector<TYPE>& b){
  typedef typename DCO_MODE::external_adjoint_object_t DCO_EAO_TYPE;

  const size_t n=b.size();
  DCO_EAO_TYPE *D = DCO_MODE::global_tape->template create_callback_object<DCO_EAO_TYPE>();

  D->write_data(&LU);
  for (size_t i=0;i<n*n;i++) D->register_input(LU[i]);
  for (size_t i=0;i<n;i++) D->register_input(b[i]);
  DCO_MODE::global_tape->switch_to_passive();
  Solve(LU,b);
  DCO_MODE::global_tape->switch_to_active();
  for(size_t i=0;i<n;i++) { 
    D->write_data(value(b[i]));
    b[i]=D->register_output(value(b[i]));
  }
  DCO_MODE::global_tape->insert_callback(Solve_fill_gap<DCO_MODE>,D);
}

template <>
inline void sim(const vector<DCO_TYPE>& c, const size_t m, vector<DCO_TYPE>& T) {
  const size_t n=c.size();
  static vector<DCO_TYPE> A(n*n);
  residual_jacobian(c,T,A);
  for (size_t i=0;i<n;++i) {
    for (size_t j=0;j<n;++j)
      A[i*n+j]=A[i*n+j]/m;
    A[i+i*n]=A[i+i*n]-1;
  }
  LUDecomp_make_gap<DCO_MODE>(A);	
  for (size_t j=0;j<m;++j) {
    for (size_t i=0;i<n;++i) T[i]=-T[i];
    Solve_make_gap<DCO_MODE>(A,T);
  }
}

int main(int argc, char* argv[]){
  if (argc!=3) {
    cerr << "2 parameters expected:" << endl
	 << "  1. number of spatial finite difference grid points" << endl
	 << "  2. number of implicit Euler steps" << endl;
    return -1;
  }
  size_t n=atoi(argv[1]), m=atoi(argv[2]);
  vector<double> c(n);
  for (size_t i=0;i<n;i++) c[i]=0.01;
  vector<double> T(n);
  for (size_t i=0;i<n-1;i++) T[i]=300.;
  T[n-1]=1700.;
  ifstream ifs("O.txt");
  vector<double> O(n);
  for (size_t i=0;i<n;i++) ifs >> O[i] ;
  vector<vector<double> > d2vdc2(n,vector<double>(n,0));

  vector<DCO_TYPE> ca(n);
  vector<DCO_TYPE> Ta(n);
  DCO_TYPE va;
  DCO_MODE::global_tape=DCO_TAPE_TYPE::create();

  for(size_t i=0;i<n;i++) {
    ca[i]=c[i];
    Ta[i]=T[i];
    DCO_MODE::global_tape->register_variable(ca[i]);
  }
  DCO_TAPE_POSITION_TYPE p=DCO_MODE::global_tape->get_position();
  for(size_t i=0;i<n;i++) {
    for(size_t j=0;j<n;j++) Ta[j]=T[j];
    if (i>0) DCO_MODE::global_tape->zero_adjoints();
    derivative(value(ca[i]))=1;
    f(ca,m,Ta,O,va);
    value(derivative(va))=1;
    DCO_MODE::global_tape->interpret_adjoint();
    for(size_t j=0;j<n;j++) derivative(value(ca[j]))=0;
    for(size_t j=0;j<n;j++) d2vdc2[j][i]=derivative(derivative(ca[j]));
    DCO_MODE::global_tape->reset_to(p);
  }
  DCO_TAPE_TYPE::remove(DCO_MODE::global_tape);
  cout.precision(15);
  for(size_t i=0;i<n;i++)
    for(size_t j=0;j<n;j++)
      cout << "d2vdc2[" << i << "][" << j << "]=" << d2vdc2[i][j] << endl;

  return 0;
}
