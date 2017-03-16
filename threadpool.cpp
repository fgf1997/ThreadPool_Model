// system header files
#include <stdio.h> 
#include <stdlib.h> 
#include <unistd.h>
#include <errno.h> 

// user-defined header files
#include "threadpool.h" 

//===============================================================
//														CThread
//�����࣬�����߳���Ļ��࣬�ṩһ���ӿ�Run
//Linux���̵߳İ�װ����װ��Linux�߳��ʹ�õ����Ժͷ���
//===============================================================
void* GlobalThreadFunction(void* argv)
{
	CThread* thr = reinterpret_cast<CThread*>(argv);
	if (thr) {
		thr->SetThreadState(THREAD_RUNNING);
		thr->Run();
		thr->SetThreadState(THREAD_FINISHED);
	}
	return NULL;
}

CThread::CThread()
{
	m_CreateSuspended = false;
	SetThreadState(THREAD_IDLE);
	m_ThreadName = NULL;
	m_Detach = false;
	sem_init(&m_ThreadSemaphore,false,0);
}

CThread::CThread(bool createsuspended,bool detach)
{
	m_CreateSuspended = createsuspended;
	SetThreadState(THREAD_IDLE);
	m_ThreadName = NULL;
	m_Detach = detach;
	sem_init(&m_ThreadSemaphore,false,0);
}

CThread::~CThread()
{
	if(NULL != m_ThreadName)
		free(m_ThreadName);
	SetThreadState(THREAD_FINISHED);
	sem_destroy(&m_ThreadSemaphore);
}

bool CThread::Terminate(void)
{
	if(pthread_cancel(m_ThreadID)!=0) {
		SetThreadState(THREAD_TERMINATED);
		SetErrcode(Error_ThreadTerminated);
		return false;
	} else
		return true;
}

bool CThread::Start(void)
{
	if(m_CreateSuspended) {
		SetThreadState(THREAD_SUSPENDED);
		sem_wait(&m_ThreadSemaphore);
	}
	int result = pthread_create(&m_ThreadID,NULL,GlobalThreadFunction,this);
	if (result == 0) {
		if (m_Detach)
			pthread_detach(m_ThreadID);
		return true;
	} else {
		SetThreadState(THREAD_DEAD);
		return false;
	}
	return true;
}

void* CThread::ThreadFunction(void* argv)
{
	CThread* thr = reinterpret_cast<CThread*>(argv);
	if (thr){
		thr->SetThreadState(THREAD_RUNNING);
		thr->Run();
		thr->SetThreadState(THREAD_FINISHED);
	}
	return NULL;
}

void CThread::Exit(void)
{
	SetThreadState(THREAD_FINISHED);
	pthread_exit((void**)0);
}

bool CThread::Wakeup(void)
{
	if(sem_post(&m_ThreadSemaphore)!=0) {
		SetErrcode(Error_ThreadWakeup);
		return false;
	} else {
		SetThreadState(THREAD_RUNNING);
		return true;
	}
}

bool CThread::SetPriority(int priority)
{
	if(priority >10 || priority <0) {
		SetErrcode(Error_ThreadSetPriority);
		return false;
	}
	
	struct sched_param threadParameter;
	threadParameter.sched_priority = priority;
	
	if (pthread_setschedparam(m_ThreadID, SCHED_OTHER, &threadParameter) != 0) {
		SetErrcode(Error_ThreadSetPriority);
		return false;
	} else
		return true;
}

int CThread::GetPriority(void)
{
	struct sched_param threadParameter;
	int policy = 0;
	if(pthread_getschedparam(m_ThreadID, &policy, &threadParameter) == 0) {
		return -1;
	} else
		return threadParameter.sched_priority;
}

bool CThread::Detach(void)
{
	if(pthread_detach(m_ThreadID)!=0) {
		SetErrcode(Error_ThreadDetach);
		return false;
	} else
		return true;
}

bool CThread::Join(void)
{
	if(pthread_join(m_ThreadID,NULL)!=0) {
		SetErrcode(Error_ThreadJoin);
		return false;
	} else
		return true;
}

bool CThread::Yield(void)
{
	if(sched_yield()!=0) {
		SetErrcode(Error_ThreadYield);
		return false;	
	} else
		return true;
}

int CThread::Self(void)
{
	return pthread_self();
}

int CThread::SetConcurrency(int num)
{
	return pthread_setconcurrency(num);
}

int CThread::GetConcurrency(void)
{
	return pthread_getconcurrency();
}

//===============================================================
//														CJob
//��������Ļ���,�ṩһ���ӿ�Run,���������඼����Ӹ���̳У�ͬʱʵ��Run����
//�÷���ʵ�־���������߼�
//===============================================================	
CJob::CJob(void)
	: m_pWorkThread(NULL),
		m_JobNo(0),
		m_JobName(NULL) {
}

CJob::~CJob()
{
	if(m_JobName != NULL)
		free(m_JobName);
}

bool CJob::GetTerminated(void)
{
	return false;
}

void CJob::SetJobName(char* jobname)
{
	if(m_JobName != NULL) {
		free(m_JobName);
		m_JobName = NULL;
	}
	if(jobname != NULL) {
		m_JobName = (char *)malloc(strlen(jobname)+1);
		strcpy(m_JobName, jobname);
	}
}

//===============================================================
//														CWorkerThread
//ʵ�ʱ����Ⱥ�ִ�е��߳��࣬��CThread�̳У�ʵ��CThread�е�Run����
//ʵ��ִ���û��ύ�ľ�������
//===============================================================	
CWorkerThread::CWorkerThread()
{
	m_Job = NULL;
	m_JobData = NULL;
	m_ThreadPool = NULL;
	m_IsEnd = false;
}

CWorkerThread::~CWorkerThread()
{
	if(m_Job != NULL)
		delete m_Job;
	if(m_ThreadPool != NULL)
		delete m_ThreadPool;
}

void CWorkerThread::Run()
{
	SetThreadState(THREAD_RUNNING);
	while(1){
		//��û�н��յ�ʵ������ʱ��m_JobΪNULL���̵߳���Wait�ȴ������ڹ���״̬
		while(m_Job == NULL) {
			m_JobCond.Wait();
		}
		m_Job->Run(m_JobData);
		m_Job->SetWorkThread(NULL);
		m_Job = NULL;
		m_ThreadPool->MoveToIdleList(this);
		//�߳�ִ����������󷵻ؿ��ж���ǰ����Ե�ǰ���ж����е��߳���Ŀ�����жϣ�
		//��������߳���Ŀ����m_AvailHigh���������ؿ��ܹ��ᣬ�б�Ҫɾ������Ŀ����̣߳����̵߳���Ϊm_InitNum��
		if(m_ThreadPool->m_IdleList.size() > m_ThreadPool->GetAvailHighNum()) {
			m_ThreadPool->DeleteIdleThread(m_ThreadPool->m_IdleList.size() - m_ThreadPool->GetInitNum());
		}
	}
}

void CWorkerThread::SetJob(CJob* job, void* jobdata)
{
	m_VarMutex.Lock();
	m_Job = job;
	m_JobData = jobdata;
	job->SetWorkThread(this);
	m_VarMutex.Unlock();
	//���ѵõ�������߳�
	m_JobCond.Signal();
}

void CWorkerThread::SetThreadPool(CThreadPool* threadpool)
{
	m_VarMutex.Lock();
	m_ThreadPool = threadpool;
	m_VarMutex.Unlock();
}

//===============================================================
//														CThreadPool
//�̳߳��࣬���𱣴��߳�(STL Vector)���ͷ��̣߳������߳�
//����������̣߳������û��ύ������ַ���CWorkerThread
//��CThreadPool�д�����������һ���ǿ���(Idle)����,һ����æµ(Busy)����,
//���������д�����еĿ����̣߳����߳�ִ������ʱ����״̬��Ϊæµ��ͬʱ�ӿ���������ɾ����������æµ������
//===============================================================	
CThreadPool::CThreadPool()
{
	m_MaxNum = 50;
	m_AvailLow = 5;
	m_AvailNum = m_InitNum = 10;
	m_AvailHigh = 20;
	
	m_BusyList.clear();
	m_IdleList.clear();
	for(unsigned int i = 0; i < m_InitNum; i++) {
		CWorkerThread* workerthr = new CWorkerThread();
		workerthr->SetThreadPool(this);
		AppendToIdleList(workerthr);
		workerthr->Start();
	}
	fprintf(stdout, "Create Worker_Thread num:[%d] Success....\n", m_InitNum);
}

CThreadPool::CThreadPool(int initnum)
{
	assert(initnum > 0 && initnum <= 30);
	m_MaxNum = 30;
	m_AvailLow = initnum-10>0?initnum-10:5;
	m_AvailNum = m_InitNum = initnum;
	m_AvailHigh = initnum + 10;
	
	m_BusyList.clear();
	m_IdleList.clear();
	//����m_InitNum�����Ĺ����߳�
	for(unsigned int i = 0; i < m_InitNum; i++) {
		CWorkerThread* workerthr = new CWorkerThread();
		workerthr->SetThreadPool(this);
		AppendToIdleList(workerthr);
		//����Start()���������̣߳����ջ����CWorkerThread::Run()����
		workerthr->Start();
	}
	fprintf(stdout, "Create Worker_Thread num:[%d] Success....\n", m_InitNum);
}

void CThreadPool::TerminateAll() 
{
	for(unsigned int i = 0; i < m_ThreadList.size(); i++) {
		CWorkerThread* workerthr = m_ThreadList[i];
		workerthr->Join();
	}
	return;
}

CThreadPool::~CThreadPool()
{
	TerminateAll();
}

CWorkerThread* CThreadPool::GetIdleThread(void)
{
	while(m_IdleList.size() == 0)
	m_IdleCond.Wait();
	m_IdleMutex.Lock();
	if(m_IdleList.size() > 0) {
		CWorkerThread* workerthr = (CWorkerThread*)m_IdleList.front();
		//fprintf(stdout, "Get Idle thread %ul\n", workerthr->GetThreadID());
		m_IdleMutex.Unlock();
		return workerthr;
	}
	m_IdleMutex.Unlock();
	return NULL;
}

void CThreadPool::AppendToIdleList(CWorkerThread* jobthread)
{
	m_IdleMutex.Lock();
	m_IdleList.push_back(jobthread);
	m_ThreadList.push_back(jobthread);
	m_IdleMutex.Unlock();
}

void CThreadPool::MoveToBusyList(CWorkerThread* idlethread)
{
	m_BusyMutex.Lock();
	m_BusyList.push_back(idlethread);
	m_AvailNum--;
	m_BusyMutex.Unlock();
	
	m_IdleMutex.Lock();
	vector<CWorkerThread*>::iterator pos;
	pos = find(m_IdleList.begin(), m_IdleList.end(), idlethread);
	if(pos != m_IdleList.end())
		m_IdleList.erase(pos);
	m_IdleMutex.Unlock();
}

void CThreadPool::MoveToIdleList(CWorkerThread* busythread)
{
	m_IdleMutex.Lock();
	m_IdleList.push_back(busythread);
	m_AvailNum++;
	m_IdleMutex.Unlock();
	m_BusyMutex.Lock();
	vector<CWorkerThread*>::iterator pos;
	pos = find(m_BusyList.begin(), m_BusyList.end(), busythread);
	if(pos != m_BusyList.end())
		m_BusyList.erase(pos);
	m_BusyMutex.Unlock();
	m_IdleCond.Signal();
	m_MaxNumCond.Signal();
}

void CThreadPool::CreateIdleThread(int num)
{
	for(int i = 0; i < num; i++) {
		CWorkerThread* workerthr = new CWorkerThread();
		workerthr->SetThreadPool(this);
		AppendToIdleList(workerthr);
		m_VarMutex.Lock();
		m_AvailNum++;
		m_VarMutex.Unlock();
		workerthr->Start();
	}
}
	
void CThreadPool::DeleteIdleThread(int num)
{
	//fprintf(stdout, "Enter into CThreadPool::DeleteIdleThread\n");
	m_IdleMutex.Lock();
	for(int i = 0; i < num; i++) {
		CWorkerThread* workerthr;
		if(m_IdleList.size() > 0) {
			workerthr = (CWorkerThread*)m_IdleList.front();
			//fprintf(stdout, "Get Idle thread%d\n", workerthr->GetThreadID());
		}
		vector<CWorkerThread*>::iterator pos;
		pos = find(m_IdleList.begin(), m_IdleList.end(), workerthr);
		if(pos != m_IdleList.end())
			m_IdleList.erase(pos);
		m_AvailNum--;
		//fprintf(stdout, "The idle thread available num %d\n",m_AvailNum);
		//fprintf(stdout, "The idlelist num:%d\n",m_IdleList.size());
	}
	m_IdleMutex.Unlock();
}

void CThreadPool::Run(CJob* job, void* jobdata)
{
	assert(job!=NULL);
	//�̳߳��ڽ��ܵ��µ�����֮���̳߳�Ҫ��鵱ǰ����æµ״̬���߳��Ƿ�ﵽ���趨�����ֵm_MaxNum��
	//����ﵽ�ˣ�����Ŀǰû�п����߳̿��ã����Ҳ��ܴ����µ��̣߳�����ȴ�ֱ�����߳�ִ����Ϸ��ص����ж�����
	if(GetBusyNum() == m_MaxNum) {
		//zrpc_log_info(0,"*ThreadPool is all Busy, Please Wait\n");
		m_MaxNumCond.Wait();
	}
	//��������߳���Ŀ����m_AvailLow���������ؿ��ܹ��أ��б�Ҫ���ӿ����̳߳ص���Ŀ�����̵߳���Ϊm_InitNum��
	if(m_IdleList.size() < m_AvailLow) {
		if(GetAllNum() + m_InitNum - m_IdleList.size() < m_MaxNum)
			CreateIdleThread(m_InitNum - m_IdleList.size());
		else	//���п����߳����������ϴ�������߳������ܳ���m_MaxNum
			CreateIdleThread(m_MaxNum - GetAllNum());
	}
	//����GetIdleThread���ҿ����߳�
	CWorkerThread* idlethr = GetIdleThread();
	if(idlethr != NULL) {
		//���߳�����æµ����
		MoveToBusyList(idlethr);
		idlethr->SetThreadPool(this);
		job->SetWorkThread(idlethr);
		//������ָ�ɸ������߳�
		idlethr->SetJob(job,jobdata);
	}
}

//===============================================================
//														CThreadManage
//�̳߳����û���ֱ�ӽӿڣ������ڲ�����ʵ�֣��ڲ�����CThreadPool����ز�����
//������Ҫ�������̳߳�ʼ�������������û��ύ�����񣨾���ǳ���
//===============================================================		
CThreadManage::CThreadManage()
{
	m_NumOfThread = 10;
	m_Poll = new CThreadPool(m_NumOfThread);
}

CThreadManage::CThreadManage(int num)
{
	m_NumOfThread = num;
	m_Poll = new CThreadPool(m_NumOfThread);
}

CThreadManage::~CThreadManage()
{
	if(m_Poll != NULL)
		delete m_Poll;
}

void CThreadManage::SetParallelNum(int num)
{
	m_NumOfThread = num;
}

void CThreadManage::Run(CJob *job, void *jobdata)
{
	m_Poll->Run(job, jobdata);
}

void CThreadManage::TerminateAll(void)
{
	m_Poll->TerminateAll();
}