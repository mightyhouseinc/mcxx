! <testinfo>
! test_generator=config/mercurium-fortran
! </testinfo>
PROGRAM MAIN
   INTERFACE FOO
     FUNCTION FOO_A(X)
       INTEGER :: FOO_A
       INTEGER, ALLOCATABLE :: X(:)
     END FUNCTION FOO_A

     FUNCTION FOO_B(X)
       REAL :: FOO_B
       INTEGER, POINTER :: X(:)
     END FUNCTION FOO_B
   END INTERFACE FOO

   INTERFACE
       SUBROUTINE WANT_INTEGER(X)
           INTEGER :: X
       END SUBROUTINE WANT_INTEGER

       SUBROUTINE WANT_REAL(X)
           REAL :: X
       END SUBROUTINE WANT_REAL
   END INTERFACE

   INTEGER, ALLOCATABLE :: A(:)
   INTEGER, POINTER :: B(:)

   CALL WANT_INTEGER( FOO(A) )
   CALL WANT_REAL( FOO(B) )
END PROGRAM MAIN
