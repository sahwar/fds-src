Rem
Rem Copyright (c) 2006 ZeroC, Inc. All rights reserved.
Rem
Rem This copy of Ice is licensed to you under the terms described in the
Rem ICE_LICENSE file included in this distribution.

Rem
Rem Create object types and views for the DEPT and EMP tables
Rem

SET ECHO ON 

CONNECT scott/tiger@orcl

DROP VIEW EMP_VIEW;
DROP VIEW DEPT_VIEW;
DROP TYPE EMP_T;
DROP TYPE DEPT_T;

CREATE TYPE DEPT_T AS OBJECT(
   DEPTNO NUMBER(2),
   DNAME VARCHAR2(14),
   LOC VARCHAR2(13));
/

CREATE TYPE EMP_T;
/
CREATE TYPE EMP_T AS OBJECT(
   EMPNO NUMBER(4),
   ENAME VARCHAR2(10),
   JOB VARCHAR2(9),
   MGRREF REF EMP_T,
   HIREDATE DATE,
   SAL NUMBER(7,2),
   COMM NUMBER(7,2),
   DEPTREF REF DEPT_T);
/

CREATE VIEW DEPT_VIEW OF DEPT_T WITH OBJECT IDENTIFIER(DEPTNO)
   AS SELECT DEPTNO, DNAME, LOC FROM DEPT;

Rem
Rem Need to create the view in two steps since it references itself
Rem
CREATE VIEW EMP_VIEW OF EMP_T WITH OBJECT IDENTIFIER(EMPNO)
   AS SELECT EMPNO, ENAME, JOB, NULL, HIREDATE, SAL, COMM, 
             MAKE_REF(DEPT_VIEW, DEPTNO) FROM EMP;

CREATE OR REPLACE VIEW EMP_VIEW OF EMP_T WITH OBJECT IDENTIFIER(EMPNO)
   AS SELECT EMPNO, ENAME, JOB, MAKE_REF(EMP_VIEW, MGR), HIREDATE, SAL,
             COMM, MAKE_REF(DEPT_VIEW, DEPTNO) FROM EMP;

COMMIT;
EXIT
