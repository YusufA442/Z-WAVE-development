
#include "main.h"

//${CONFIG_BEGIN}
#define CFG_BINARY_FILES *.bin|*.dat
#define CFG_BRL_DATABUFFER_IMPLEMENTED 1
#define CFG_BRL_FILESTREAM_IMPLEMENTED 1
#define CFG_BRL_GAMETARGET_IMPLEMENTED 1
#define CFG_BRL_SOCKET_IMPLEMENTED 1
#define CFG_BRL_STREAM_IMPLEMENTED 1
#define CFG_BRL_THREAD_IMPLEMENTED 1
#define CFG_CD 
#define CFG_CONFIG debug
#define CFG_CPP_GC_MODE 1
#define CFG_GLFW_SWAP_INTERVAL -1
#define CFG_GLFW_USE_MINGW 1
#define CFG_GLFW_VERSION 2
#define CFG_GLFW_WINDOW_FULLSCREEN 0
#define CFG_GLFW_WINDOW_HEIGHT 480
#define CFG_GLFW_WINDOW_RESIZABLE 0
#define CFG_GLFW_WINDOW_TITLE Z-WAVE
#define CFG_GLFW_WINDOW_WIDTH 640
#define CFG_HOST winnt
#define CFG_IMAGE_FILES *.png|*.jpg
#define CFG_LANG cpp
#define CFG_MODPATH 
#define CFG_MOJO_AUTO_SUSPEND_ENABLED 1
#define CFG_MOJO_DRIVER_IMPLEMENTED 1
#define CFG_MOJO_IMAGE_FILTERING_ENABLED 1
#define CFG_MUSIC_FILES *.wav|*.ogg
#define CFG_OPENGL_DEPTH_BUFFER_ENABLED 0
#define CFG_OPENGL_GLES20_ENABLED 0
#define CFG_SAFEMODE 0
#define CFG_SOUND_FILES *.wav|*.ogg
#define CFG_TARGET glfw
#define CFG_TEXT_FILES *.ttf;*.txt|*.xml|*.json
//${CONFIG_END}

//${TRANSCODE_BEGIN}

#include <wctype.h>
#include <locale.h>

// C++ Monkey runtime.
//
// Placed into the public domain 24/02/2011.
// No warranty implied; use at your own risk.

//***** Monkey Types *****

typedef wchar_t Char;
template<class T> class Array;
class String;
class Object;

#if CFG_CPP_DOUBLE_PRECISION_FLOATS
typedef double Float;
#define FLOAT(X) X
#else
typedef float Float;
#define FLOAT(X) X##f
#endif

void dbg_error( const char *p );

#if !_MSC_VER
#define sprintf_s sprintf
#define sscanf_s sscanf
#endif

//***** GC Config *****

#if CFG_CPP_GC_DEBUG
#define DEBUG_GC 1
#else
#define DEBUG_GC 0
#endif

// GC mode:
//
// 0 = disabled
// 1 = Incremental GC every OnWhatever
// 2 = Incremental GC every allocation
//
#ifndef CFG_CPP_GC_MODE
#define CFG_CPP_GC_MODE 1
#endif

//How many bytes alloced to trigger GC
//
#ifndef CFG_CPP_GC_TRIGGER
#define CFG_CPP_GC_TRIGGER 8*1024*1024
#endif

//GC_MODE 2 needs to track locals on a stack - this may need to be bumped if your app uses a LOT of locals, eg: is heavily recursive...
//
#ifndef CFG_CPP_GC_MAX_LOCALS
#define CFG_CPP_GC_MAX_LOCALS 8192
#endif

// ***** GC *****

#if _WIN32

int gc_micros(){
	static int f;
	static LARGE_INTEGER pcf;
	if( !f ){
		if( QueryPerformanceFrequency( &pcf ) && pcf.QuadPart>=1000000L ){
			pcf.QuadPart/=1000000L;
			f=1;
		}else{
			f=-1;
		}
	}
	if( f>0 ){
		LARGE_INTEGER pc;
		if( QueryPerformanceCounter( &pc ) ) return pc.QuadPart/pcf.QuadPart;
		f=-1;
	}
	return 0;// timeGetTime()*1000;
}

#elif __APPLE__

#include <mach/mach_time.h>

int gc_micros(){
	static int f;
	static mach_timebase_info_data_t timeInfo;
	if( !f ){
		mach_timebase_info( &timeInfo );
		timeInfo.denom*=1000L;
		f=1;
	}
	return mach_absolute_time()*timeInfo.numer/timeInfo.denom;
}

#else

int gc_micros(){
	return 0;
}

#endif

#define gc_mark_roots gc_mark

void gc_mark_roots();

struct gc_object;

gc_object *gc_object_alloc( int size );
void gc_object_free( gc_object *p );

struct gc_object{
	gc_object *succ;
	gc_object *pred;
	int flags;
	
	virtual ~gc_object(){
	}
	
	virtual void mark(){
	}
	
	void *operator new( size_t size ){
		return gc_object_alloc( size );
	}
	
	void operator delete( void *p ){
		gc_object_free( (gc_object*)p );
	}
};

gc_object gc_free_list;
gc_object gc_marked_list;
gc_object gc_unmarked_list;
gc_object gc_queued_list;	//doesn't really need to be doubly linked...

int gc_free_bytes;
int gc_marked_bytes;
int gc_alloced_bytes;
int gc_max_alloced_bytes;
int gc_new_bytes;
int gc_markbit=1;

gc_object *gc_cache[8];

void gc_collect_all();
void gc_mark_queued( int n );

#define GC_CLEAR_LIST( LIST ) ((LIST).succ=(LIST).pred=&(LIST))

#define GC_LIST_IS_EMPTY( LIST ) ((LIST).succ==&(LIST))

#define GC_REMOVE_NODE( NODE ){\
(NODE)->pred->succ=(NODE)->succ;\
(NODE)->succ->pred=(NODE)->pred;}

#define GC_INSERT_NODE( NODE,SUCC ){\
(NODE)->pred=(SUCC)->pred;\
(NODE)->succ=(SUCC);\
(SUCC)->pred->succ=(NODE);\
(SUCC)->pred=(NODE);}

void gc_init1(){
	GC_CLEAR_LIST( gc_free_list );
	GC_CLEAR_LIST( gc_marked_list );
	GC_CLEAR_LIST( gc_unmarked_list);
	GC_CLEAR_LIST( gc_queued_list );
}

void gc_init2(){
	gc_mark_roots();
}

#if CFG_CPP_GC_MODE==2

int gc_ctor_nest;
gc_object *gc_locals[CFG_CPP_GC_MAX_LOCALS],**gc_locals_sp=gc_locals;

struct gc_ctor{
	gc_ctor(){ ++gc_ctor_nest; }
	~gc_ctor(){ --gc_ctor_nest; }
};

struct gc_enter{
	gc_object **sp;
	gc_enter():sp(gc_locals_sp){
	}
	~gc_enter(){
#if DEBUG_GC
		static int max_locals;
		int n=gc_locals_sp-gc_locals;
		if( n>max_locals ){
			max_locals=n;
			printf( "max_locals=%i\n",n );
		}
#endif		
		gc_locals_sp=sp;
	}
};

#define GC_CTOR gc_ctor _c;
#define GC_ENTER gc_enter _e;

#else

struct gc_ctor{
};
struct gc_enter{
};

#define GC_CTOR
#define GC_ENTER

#endif

//Can be modified off thread!
static volatile int gc_ext_new_bytes;

#if _MSC_VER
#define atomic_add(P,V) InterlockedExchangeAdd((volatile unsigned int*)P,V)			//(*(P)+=(V))
#define atomic_sub(P,V) InterlockedExchangeSubtract((volatile unsigned int*)P,V)	//(*(P)-=(V))
#else
#define atomic_add(P,V) __sync_fetch_and_add(P,V)
#define atomic_sub(P,V) __sync_fetch_and_sub(P,V)
#endif

//Careful! May be called off thread!
//
void gc_ext_malloced( int size ){
	atomic_add( &gc_ext_new_bytes,size );
}

void gc_object_free( gc_object *p ){

	int size=p->flags & ~7;
	gc_free_bytes-=size;
	
	if( size<64 ){
		p->succ=gc_cache[size>>3];
		gc_cache[size>>3]=p;
	}else{
		free( p );
	}
}

void gc_flush_free( int size ){

	int t=gc_free_bytes-size;
	if( t<0 ) t=0;
	
	while( gc_free_bytes>t ){
	
		gc_object *p=gc_free_list.succ;

		GC_REMOVE_NODE( p );

#if DEBUG_GC
//		printf( "deleting @%p\n",p );fflush( stdout );
//		p->flags|=4;
//		continue;
#endif
		delete p;
	}
}

gc_object *gc_object_alloc( int size ){

	size=(size+7)&~7;
	
	gc_new_bytes+=size;
	
#if CFG_CPP_GC_MODE==2

	if( !gc_ctor_nest ){

#if DEBUG_GC
		int ms=gc_micros();
#endif
		if( gc_new_bytes+gc_ext_new_bytes>(CFG_CPP_GC_TRIGGER) ){
			atomic_sub( &gc_ext_new_bytes,gc_ext_new_bytes );
			gc_collect_all();
			gc_new_bytes=0;
		}else{
			gc_mark_queued( (long long)(gc_new_bytes)*(gc_alloced_bytes-gc_new_bytes)/(CFG_CPP_GC_TRIGGER)+gc_new_bytes );
		}

#if DEBUG_GC
		ms=gc_micros()-ms;
		if( ms>=100 ) {printf( "gc time:%i\n",ms );fflush( stdout );}
#endif
	}
	
#endif

	gc_flush_free( size );

	gc_object *p;
	if( size<64 && (p=gc_cache[size>>3]) ){
		gc_cache[size>>3]=p->succ;
	}else{
		p=(gc_object*)malloc( size );
	}
	
	p->flags=size|gc_markbit;
	GC_INSERT_NODE( p,&gc_unmarked_list );

	gc_alloced_bytes+=size;
	if( gc_alloced_bytes>gc_max_alloced_bytes ) gc_max_alloced_bytes=gc_alloced_bytes;
	
#if CFG_CPP_GC_MODE==2
	*gc_locals_sp++=p;
#endif

	return p;
}

#if DEBUG_GC

template<class T> gc_object *to_gc_object( T *t ){
	gc_object *p=dynamic_cast<gc_object*>(t);
	if( p && (p->flags & 4) ){
		printf( "gc error : object already deleted @%p\n",p );fflush( stdout );
		exit(-1);
	}
	return p;
}

#else

#define to_gc_object(t) dynamic_cast<gc_object*>(t)

#endif

template<class T> T *gc_retain( T *t ){
#if CFG_CPP_GC_MODE==2
	*gc_locals_sp++=to_gc_object( t );
#endif
	return t;
}

template<class T> void gc_mark( T *t ){

	gc_object *p=to_gc_object( t );
	
	if( p && (p->flags & 3)==gc_markbit ){
		p->flags^=1;
		GC_REMOVE_NODE( p );
		GC_INSERT_NODE( p,&gc_marked_list );
		gc_marked_bytes+=(p->flags & ~7);
		p->mark();
	}
}

template<class T> void gc_mark_q( T *t ){

	gc_object *p=to_gc_object( t );
	
	if( p && (p->flags & 3)==gc_markbit ){
		p->flags^=1;
		GC_REMOVE_NODE( p );
		GC_INSERT_NODE( p,&gc_queued_list );
	}
}

template<class T,class V> void gc_assign( T *&lhs,V *rhs ){

	gc_object *p=to_gc_object( rhs );
	
	if( p && (p->flags & 3)==gc_markbit ){
		p->flags^=1;
		GC_REMOVE_NODE( p );
		GC_INSERT_NODE( p,&gc_queued_list );
	}
	lhs=rhs;
}

void gc_mark_locals(){

#if CFG_CPP_GC_MODE==2
	for( gc_object **pp=gc_locals;pp!=gc_locals_sp;++pp ){
		gc_object *p=*pp;
		if( p && (p->flags & 3)==gc_markbit ){
			p->flags^=1;
			GC_REMOVE_NODE( p );
			GC_INSERT_NODE( p,&gc_marked_list );
			gc_marked_bytes+=(p->flags & ~7);
			p->mark();
		}
	}
#endif	
}

void gc_mark_queued( int n ){
	while( gc_marked_bytes<n && !GC_LIST_IS_EMPTY( gc_queued_list ) ){
		gc_object *p=gc_queued_list.succ;
		GC_REMOVE_NODE( p );
		GC_INSERT_NODE( p,&gc_marked_list );
		gc_marked_bytes+=(p->flags & ~7);
		p->mark();
	}
}

void gc_validate_list( gc_object &list,const char *msg ){
	gc_object *node=list.succ;
	while( node ){
		if( node==&list ) return;
		if( !node->pred ) break;
		if( node->pred->succ!=node ) break;
		node=node->succ;
	}
	if( msg ){
		puts( msg );fflush( stdout );
	}
	puts( "LIST ERROR!" );
	exit(-1);
}

//returns reclaimed bytes
void gc_sweep(){

	int reclaimed_bytes=gc_alloced_bytes-gc_marked_bytes;
	
	if( reclaimed_bytes ){
	
		//append unmarked list to end of free list
		gc_object *head=gc_unmarked_list.succ;
		gc_object *tail=gc_unmarked_list.pred;
		gc_object *succ=&gc_free_list;
		gc_object *pred=succ->pred;
		
		head->pred=pred;
		tail->succ=succ;
		pred->succ=head;
		succ->pred=tail;
		
		gc_free_bytes+=reclaimed_bytes;
	}

	//move marked to unmarked.
	if( GC_LIST_IS_EMPTY( gc_marked_list ) ){
		GC_CLEAR_LIST( gc_unmarked_list );
	}else{
		gc_unmarked_list.succ=gc_marked_list.succ;
		gc_unmarked_list.pred=gc_marked_list.pred;
		gc_unmarked_list.succ->pred=gc_unmarked_list.pred->succ=&gc_unmarked_list;
		GC_CLEAR_LIST( gc_marked_list );
	}
	
	//adjust sizes
	gc_alloced_bytes=gc_marked_bytes;
	gc_marked_bytes=0;
	gc_markbit^=1;
}

void gc_collect_all(){

//	puts( "Mark locals" );
	gc_mark_locals();

//	puts( "Marked queued" );
	gc_mark_queued( 0x7fffffff );

//	puts( "Sweep" );
	gc_sweep();

//	puts( "Mark roots" );
	gc_mark_roots();

#if DEBUG_GC
	gc_validate_list( gc_marked_list,"Validating gc_marked_list"  );
	gc_validate_list( gc_unmarked_list,"Validating gc_unmarked_list"  );
	gc_validate_list( gc_free_list,"Validating gc_free_list" );
#endif

}

void gc_collect(){
	
#if CFG_CPP_GC_MODE==1

#if DEBUG_GC
	int ms=gc_micros();
#endif

	if( gc_new_bytes+gc_ext_new_bytes>(CFG_CPP_GC_TRIGGER) ){
		atomic_sub( &gc_ext_new_bytes,gc_ext_new_bytes );
		gc_collect_all();
		gc_new_bytes=0;
	}else{
		gc_mark_queued( (long long)(gc_new_bytes)*(gc_alloced_bytes-gc_new_bytes)/(CFG_CPP_GC_TRIGGER)+gc_new_bytes );
	}

#if DEBUG_GC
	ms=gc_micros()-ms;
//	if( ms>=100 ) {printf( "gc time:%i\n",ms );fflush( stdout );}
	if( ms>10 ) {printf( "gc time:%i\n",ms );fflush( stdout );}
#endif

#endif
}

// ***** Array *****

template<class T> T *t_memcpy( T *dst,const T *src,int n ){
	memcpy( dst,src,n*sizeof(T) );
	return dst+n;
}

template<class T> T *t_memset( T *dst,int val,int n ){
	memset( dst,val,n*sizeof(T) );
	return dst+n;
}

template<class T> int t_memcmp( const T *x,const T *y,int n ){
	return memcmp( x,y,n*sizeof(T) );
}

template<class T> int t_strlen( const T *p ){
	const T *q=p++;
	while( *q++ ){}
	return q-p;
}

template<class T> T *t_create( int n,T *p ){
	t_memset( p,0,n );
	return p+n;
}

template<class T> T *t_create( int n,T *p,const T *q ){
	t_memcpy( p,q,n );
	return p+n;
}

template<class T> void t_destroy( int n,T *p ){
}

template<class T> void gc_mark_elements( int n,T *p ){
}

template<class T> void gc_mark_elements( int n,T **p ){
	for( int i=0;i<n;++i ) gc_mark( p[i] );
}

template<class T> class Array{
public:
	Array():rep( &nullRep ){
	}

	//Uses default...
//	Array( const Array<T> &t )...
	
	Array( int length ):rep( Rep::alloc( length ) ){
		t_create( rep->length,rep->data );
	}
	
	Array( const T *p,int length ):rep( Rep::alloc(length) ){
		t_create( rep->length,rep->data,p );
	}
	
	~Array(){
	}

	//Uses default...
//	Array &operator=( const Array &t )...
	
	int Length()const{ 
		return rep->length; 
	}
	
	T &At( int index ){
		if( index<0 || index>=rep->length ) dbg_error( "Array index out of range" );
		return rep->data[index]; 
	}
	
	const T &At( int index )const{
		if( index<0 || index>=rep->length ) dbg_error( "Array index out of range" );
		return rep->data[index]; 
	}
	
	T &operator[]( int index ){
		return rep->data[index]; 
	}

	const T &operator[]( int index )const{
		return rep->data[index]; 
	}
	
	Array Slice( int from,int term )const{
		int len=rep->length;
		if( from<0 ){ 
			from+=len;
			if( from<0 ) from=0;
		}else if( from>len ){
			from=len;
		}
		if( term<0 ){
			term+=len;
		}else if( term>len ){
			term=len;
		}
		if( term<=from ) return Array();
		return Array( rep->data+from,term-from );
	}

	Array Slice( int from )const{
		return Slice( from,rep->length );
	}
	
	Array Resize( int newlen )const{
		if( newlen<=0 ) return Array();
		int n=rep->length;
		if( newlen<n ) n=newlen;
		Rep *p=Rep::alloc( newlen );
		T *q=p->data;
		q=t_create( n,q,rep->data );
		q=t_create( (newlen-n),q );
		return Array( p );
	}
	
private:
	struct Rep : public gc_object{
		int length;
		T data[0];
		
		Rep():length(0){
			flags=3;
		}
		
		Rep( int length ):length(length){
		}
		
		~Rep(){
			t_destroy( length,data );
		}
		
		void mark(){
			gc_mark_elements( length,data );
		}
		
		static Rep *alloc( int length ){
			if( !length ) return &nullRep;
			void *p=gc_object_alloc( sizeof(Rep)+length*sizeof(T) );
			return ::new(p) Rep( length );
		}
		
	};
	Rep *rep;
	
	static Rep nullRep;
	
	template<class C> friend void gc_mark( Array<C> t );
	template<class C> friend void gc_mark_q( Array<C> t );
	template<class C> friend Array<C> gc_retain( Array<C> t );
	template<class C> friend void gc_assign( Array<C> &lhs,Array<C> rhs );
	template<class C> friend void gc_mark_elements( int n,Array<C> *p );
	
	Array( Rep *rep ):rep(rep){
	}
};

template<class T> typename Array<T>::Rep Array<T>::nullRep;

template<class T> Array<T> *t_create( int n,Array<T> *p ){
	for( int i=0;i<n;++i ) *p++=Array<T>();
	return p;
}

template<class T> Array<T> *t_create( int n,Array<T> *p,const Array<T> *q ){
	for( int i=0;i<n;++i ) *p++=*q++;
	return p;
}

template<class T> void gc_mark( Array<T> t ){
	gc_mark( t.rep );
}

template<class T> void gc_mark_q( Array<T> t ){
	gc_mark_q( t.rep );
}

template<class T> Array<T> gc_retain( Array<T> t ){
#if CFG_CPP_GC_MODE==2
	gc_retain( t.rep );
#endif
	return t;
}

template<class T> void gc_assign( Array<T> &lhs,Array<T> rhs ){
	gc_mark( rhs.rep );
	lhs=rhs;
}

template<class T> void gc_mark_elements( int n,Array<T> *p ){
	for( int i=0;i<n;++i ) gc_mark( p[i].rep );
}
		
// ***** String *****

static const char *_str_load_err;

class String{
public:
	String():rep( &nullRep ){
	}
	
	String( const String &t ):rep( t.rep ){
		rep->retain();
	}

	String( int n ){
		char buf[256];
		sprintf_s( buf,"%i",n );
		rep=Rep::alloc( t_strlen(buf) );
		for( int i=0;i<rep->length;++i ) rep->data[i]=buf[i];
	}
	
	String( Float n ){
		char buf[256];
		
		//would rather use snprintf, but it's doing weird things in MingW.
		//
		sprintf_s( buf,"%.17lg",n );
		//
		char *p;
		for( p=buf;*p;++p ){
			if( *p=='.' || *p=='e' ) break;
		}
		if( !*p ){
			*p++='.';
			*p++='0';
			*p=0;
		}

		rep=Rep::alloc( t_strlen(buf) );
		for( int i=0;i<rep->length;++i ) rep->data[i]=buf[i];
	}

	String( Char ch,int length ):rep( Rep::alloc(length) ){
		for( int i=0;i<length;++i ) rep->data[i]=ch;
	}

	String( const Char *p ):rep( Rep::alloc(t_strlen(p)) ){
		t_memcpy( rep->data,p,rep->length );
	}

	String( const Char *p,int length ):rep( Rep::alloc(length) ){
		t_memcpy( rep->data,p,rep->length );
	}
	
#if __OBJC__	
	String( NSString *nsstr ):rep( Rep::alloc([nsstr length]) ){
		unichar *buf=(unichar*)malloc( rep->length * sizeof(unichar) );
		[nsstr getCharacters:buf range:NSMakeRange(0,rep->length)];
		for( int i=0;i<rep->length;++i ) rep->data[i]=buf[i];
		free( buf );
	}
#endif

#if __cplusplus_winrt
	String( Platform::String ^str ):rep( Rep::alloc(str->Length()) ){
		for( int i=0;i<rep->length;++i ) rep->data[i]=str->Data()[i];
	}
#endif

	~String(){
		rep->release();
	}
	
	template<class C> String( const C *p ):rep( Rep::alloc(t_strlen(p)) ){
		for( int i=0;i<rep->length;++i ) rep->data[i]=p[i];
	}
	
	template<class C> String( const C *p,int length ):rep( Rep::alloc(length) ){
		for( int i=0;i<rep->length;++i ) rep->data[i]=p[i];
	}
	
	String Copy()const{
		Rep *crep=Rep::alloc( rep->length );
		t_memcpy( crep->data,rep->data,rep->length );
		return String( crep );
	}
	
	int Length()const{
		return rep->length;
	}
	
	const Char *Data()const{
		return rep->data;
	}
	
	Char At( int index )const{
		if( index<0 || index>=rep->length ) dbg_error( "Character index out of range" );
		return rep->data[index]; 
	}
	
	Char operator[]( int index )const{
		return rep->data[index];
	}
	
	String &operator=( const String &t ){
		t.rep->retain();
		rep->release();
		rep=t.rep;
		return *this;
	}
	
	String &operator+=( const String &t ){
		return operator=( *this+t );
	}
	
	int Compare( const String &t )const{
		int n=rep->length<t.rep->length ? rep->length : t.rep->length;
		for( int i=0;i<n;++i ){
			if( int q=(int)(rep->data[i])-(int)(t.rep->data[i]) ) return q;
		}
		return rep->length-t.rep->length;
	}
	
	bool operator==( const String &t )const{
		return rep->length==t.rep->length && t_memcmp( rep->data,t.rep->data,rep->length )==0;
	}
	
	bool operator!=( const String &t )const{
		return rep->length!=t.rep->length || t_memcmp( rep->data,t.rep->data,rep->length )!=0;
	}
	
	bool operator<( const String &t )const{
		return Compare( t )<0;
	}
	
	bool operator<=( const String &t )const{
		return Compare( t )<=0;
	}
	
	bool operator>( const String &t )const{
		return Compare( t )>0;
	}
	
	bool operator>=( const String &t )const{
		return Compare( t )>=0;
	}
	
	String operator+( const String &t )const{
		if( !rep->length ) return t;
		if( !t.rep->length ) return *this;
		Rep *p=Rep::alloc( rep->length+t.rep->length );
		Char *q=p->data;
		q=t_memcpy( q,rep->data,rep->length );
		q=t_memcpy( q,t.rep->data,t.rep->length );
		return String( p );
	}
	
	int Find( String find,int start=0 )const{
		if( start<0 ) start=0;
		while( start+find.rep->length<=rep->length ){
			if( !t_memcmp( rep->data+start,find.rep->data,find.rep->length ) ) return start;
			++start;
		}
		return -1;
	}
	
	int FindLast( String find )const{
		int start=rep->length-find.rep->length;
		while( start>=0 ){
			if( !t_memcmp( rep->data+start,find.rep->data,find.rep->length ) ) return start;
			--start;
		}
		return -1;
	}
	
	int FindLast( String find,int start )const{
		if( start>rep->length-find.rep->length ) start=rep->length-find.rep->length;
		while( start>=0 ){
			if( !t_memcmp( rep->data+start,find.rep->data,find.rep->length ) ) return start;
			--start;
		}
		return -1;
	}
	
	String Trim()const{
		int i=0,i2=rep->length;
		while( i<i2 && rep->data[i]<=32 ) ++i;
		while( i2>i && rep->data[i2-1]<=32 ) --i2;
		if( i==0 && i2==rep->length ) return *this;
		return String( rep->data+i,i2-i );
	}

	Array<String> Split( String sep )const{
	
		if( !sep.rep->length ){
			Array<String> bits( rep->length );
			for( int i=0;i<rep->length;++i ){
				bits[i]=String( (Char)(*this)[i],1 );
			}
			return bits;
		}
		
		int i=0,i2,n=1;
		while( (i2=Find( sep,i ))!=-1 ){
			++n;
			i=i2+sep.rep->length;
		}
		Array<String> bits( n );
		if( n==1 ){
			bits[0]=*this;
			return bits;
		}
		i=0;n=0;
		while( (i2=Find( sep,i ))!=-1 ){
			bits[n++]=Slice( i,i2 );
			i=i2+sep.rep->length;
		}
		bits[n]=Slice( i );
		return bits;
	}

	String Join( Array<String> bits )const{
		if( bits.Length()==0 ) return String();
		if( bits.Length()==1 ) return bits[0];
		int newlen=rep->length * (bits.Length()-1);
		for( int i=0;i<bits.Length();++i ){
			newlen+=bits[i].rep->length;
		}
		Rep *p=Rep::alloc( newlen );
		Char *q=p->data;
		q=t_memcpy( q,bits[0].rep->data,bits[0].rep->length );
		for( int i=1;i<bits.Length();++i ){
			q=t_memcpy( q,rep->data,rep->length );
			q=t_memcpy( q,bits[i].rep->data,bits[i].rep->length );
		}
		return String( p );
	}

	String Replace( String find,String repl )const{
		int i=0,i2,newlen=0;
		while( (i2=Find( find,i ))!=-1 ){
			newlen+=(i2-i)+repl.rep->length;
			i=i2+find.rep->length;
		}
		if( !i ) return *this;
		newlen+=rep->length-i;
		Rep *p=Rep::alloc( newlen );
		Char *q=p->data;
		i=0;
		while( (i2=Find( find,i ))!=-1 ){
			q=t_memcpy( q,rep->data+i,i2-i );
			q=t_memcpy( q,repl.rep->data,repl.rep->length );
			i=i2+find.rep->length;
		}
		q=t_memcpy( q,rep->data+i,rep->length-i );
		return String( p );
	}

	String ToLower()const{
		for( int i=0;i<rep->length;++i ){
			Char t=towlower( rep->data[i] );
			if( t==rep->data[i] ) continue;
			Rep *p=Rep::alloc( rep->length );
			Char *q=p->data;
			t_memcpy( q,rep->data,i );
			for( q[i++]=t;i<rep->length;++i ){
				q[i]=towlower( rep->data[i] );
			}
			return String( p );
		}
		return *this;
	}

	String ToUpper()const{
		for( int i=0;i<rep->length;++i ){
			Char t=towupper( rep->data[i] );
			if( t==rep->data[i] ) continue;
			Rep *p=Rep::alloc( rep->length );
			Char *q=p->data;
			t_memcpy( q,rep->data,i );
			for( q[i++]=t;i<rep->length;++i ){
				q[i]=towupper( rep->data[i] );
			}
			return String( p );
		}
		return *this;
	}
	
	bool Contains( String sub )const{
		return Find( sub )!=-1;
	}

	bool StartsWith( String sub )const{
		return sub.rep->length<=rep->length && !t_memcmp( rep->data,sub.rep->data,sub.rep->length );
	}

	bool EndsWith( String sub )const{
		return sub.rep->length<=rep->length && !t_memcmp( rep->data+rep->length-sub.rep->length,sub.rep->data,sub.rep->length );
	}
	
	String Slice( int from,int term )const{
		int len=rep->length;
		if( from<0 ){
			from+=len;
			if( from<0 ) from=0;
		}else if( from>len ){
			from=len;
		}
		if( term<0 ){
			term+=len;
		}else if( term>len ){
			term=len;
		}
		if( term<from ) return String();
		if( from==0 && term==len ) return *this;
		return String( rep->data+from,term-from );
	}

	String Slice( int from )const{
		return Slice( from,rep->length );
	}
	
	Array<int> ToChars()const{
		Array<int> chars( rep->length );
		for( int i=0;i<rep->length;++i ) chars[i]=rep->data[i];
		return chars;
	}
	
	int ToInt()const{
		char buf[64];
		return atoi( ToCString<char>( buf,sizeof(buf) ) );
	}
	
	Float ToFloat()const{
		char buf[256];
		return atof( ToCString<char>( buf,sizeof(buf) ) );
	}

	template<class C> class CString{
		struct Rep{
			int refs;
			C data[1];
		};
		Rep *_rep;
		static Rep _nul;
	public:
		template<class T> CString( const T *data,int length ){
			_rep=(Rep*)malloc( length*sizeof(C)+sizeof(Rep) );
			_rep->refs=1;
			_rep->data[length]=0;
			for( int i=0;i<length;++i ){
				_rep->data[i]=(C)data[i];
			}
		}
		CString():_rep( new Rep ){
			_rep->refs=1;
		}
		CString( const CString &c ):_rep(c._rep){
			++_rep->refs;
		}
		~CString(){
			if( !--_rep->refs ) free( _rep );
		}
		CString &operator=( const CString &c ){
			++c._rep->refs;
			if( !--_rep->refs ) free( _rep );
			_rep=c._rep;
			return *this;
		}
		operator const C*()const{ 
			return _rep->data;
		}
	};
	
	template<class C> CString<C> ToCString()const{
		return CString<C>( rep->data,rep->length );
	}

	template<class C> C *ToCString( C *p,int length )const{
		if( --length>rep->length ) length=rep->length;
		for( int i=0;i<length;++i ) p[i]=rep->data[i];
		p[length]=0;
		return p;
	}
	
#if __OBJC__	
	NSString *ToNSString()const{
		return [NSString stringWithCharacters:ToCString<unichar>() length:rep->length];
	}
#endif

#if __cplusplus_winrt
	Platform::String ^ToWinRTString()const{
		return ref new Platform::String( rep->data,rep->length );
	}
#endif
	CString<char> ToUtf8()const{
		std::vector<unsigned char> buf;
		Save( buf );
		return CString<char>( &buf[0],buf.size() );
	}

	bool Save( FILE *fp )const{
		std::vector<unsigned char> buf;
		Save( buf );
		return buf.size() ? fwrite( &buf[0],1,buf.size(),fp )==buf.size() : true;
	}
	
	void Save( std::vector<unsigned char> &buf )const{
	
		Char *p=rep->data;
		Char *e=p+rep->length;
		
		while( p<e ){
			Char c=*p++;
			if( c<0x80 ){
				buf.push_back( c );
			}else if( c<0x800 ){
				buf.push_back( 0xc0 | (c>>6) );
				buf.push_back( 0x80 | (c & 0x3f) );
			}else{
				buf.push_back( 0xe0 | (c>>12) );
				buf.push_back( 0x80 | ((c>>6) & 0x3f) );
				buf.push_back( 0x80 | (c & 0x3f) );
			}
		}
	}
	
	static String FromChars( Array<int> chars ){
		int n=chars.Length();
		Rep *p=Rep::alloc( n );
		for( int i=0;i<n;++i ){
			p->data[i]=chars[i];
		}
		return String( p );
	}

	static String Load( FILE *fp ){
		unsigned char tmp[4096];
		std::vector<unsigned char> buf;
		for(;;){
			int n=fread( tmp,1,4096,fp );
			if( n>0 ) buf.insert( buf.end(),tmp,tmp+n );
			if( n!=4096 ) break;
		}
		return buf.size() ? String::Load( &buf[0],buf.size() ) : String();
	}
	
	static String Load( unsigned char *p,int n ){
	
		_str_load_err=0;
		
		unsigned char *e=p+n;
		std::vector<Char> chars;
		
		int t0=n>0 ? p[0] : -1;
		int t1=n>1 ? p[1] : -1;

		if( t0==0xfe && t1==0xff ){
			p+=2;
			while( p<e-1 ){
				int c=*p++;
				chars.push_back( (c<<8)|*p++ );
			}
		}else if( t0==0xff && t1==0xfe ){
			p+=2;
			while( p<e-1 ){
				int c=*p++;
				chars.push_back( (*p++<<8)|c );
			}
		}else{
			int t2=n>2 ? p[2] : -1;
			if( t0==0xef && t1==0xbb && t2==0xbf ) p+=3;
			unsigned char *q=p;
			bool fail=false;
			while( p<e ){
				unsigned int c=*p++;
				if( c & 0x80 ){
					if( (c & 0xe0)==0xc0 ){
						if( p>=e || (p[0] & 0xc0)!=0x80 ){
							fail=true;
							break;
						}
						c=((c & 0x1f)<<6) | (p[0] & 0x3f);
						p+=1;
					}else if( (c & 0xf0)==0xe0 ){
						if( p+1>=e || (p[0] & 0xc0)!=0x80 || (p[1] & 0xc0)!=0x80 ){
							fail=true;
							break;
						}
						c=((c & 0x0f)<<12) | ((p[0] & 0x3f)<<6) | (p[1] & 0x3f);
						p+=2;
					}else{
						fail=true;
						break;
					}
				}
				chars.push_back( c );
			}
			if( fail ){
				_str_load_err="Invalid UTF-8";
				return String( q,n );
			}
		}
		return chars.size() ? String( &chars[0],chars.size() ) : String();
	}

private:
	
	struct Rep{
		int refs;
		int length;
		Char data[0];
		
		Rep():refs(1),length(0){
		}
		
		Rep( int length ):refs(1),length(length){
		}
		
		void retain(){
//			atomic_add( &refs,1 );
			++refs;
		}
		
		void release(){
//			if( atomic_sub( &refs,1 )>1 || this==&nullRep ) return;
			if( --refs || this==&nullRep ) return;
			free( this );
		}

		static Rep *alloc( int length ){
			if( !length ) return &nullRep;
			void *p=malloc( sizeof(Rep)+length*sizeof(Char) );
			return new(p) Rep( length );
		}
	};
	Rep *rep;
	
	static Rep nullRep;
	
	String( Rep *rep ):rep(rep){
	}
};

String::Rep String::nullRep;

String *t_create( int n,String *p ){
	for( int i=0;i<n;++i ) new( &p[i] ) String();
	return p+n;
}

String *t_create( int n,String *p,const String *q ){
	for( int i=0;i<n;++i ) new( &p[i] ) String( q[i] );
	return p+n;
}

void t_destroy( int n,String *p ){
	for( int i=0;i<n;++i ) p[i].~String();
}

// ***** Object *****

String dbg_stacktrace();

class Object : public gc_object{
public:
	virtual bool Equals( Object *obj ){
		return this==obj;
	}
	
	virtual int Compare( Object *obj ){
		return (char*)this-(char*)obj;
	}
	
	virtual String debug(){
		return "+Object\n";
	}
};

class ThrowableObject : public Object{
#ifndef NDEBUG
public:
	String stackTrace;
	ThrowableObject():stackTrace( dbg_stacktrace() ){}
#endif
};

struct gc_interface{
	virtual ~gc_interface(){}
};

//***** Debugger *****

//#define Error bbError
//#define Print bbPrint

int bbPrint( String t );

#define dbg_stream stderr

#if _MSC_VER
#define dbg_typeof decltype
#else
#define dbg_typeof __typeof__
#endif 

struct dbg_func;
struct dbg_var_type;

static int dbg_suspend;
static int dbg_stepmode;

const char *dbg_info;
String dbg_exstack;

static void *dbg_var_buf[65536*3];
static void **dbg_var_ptr=dbg_var_buf;

static dbg_func *dbg_func_buf[1024];
static dbg_func **dbg_func_ptr=dbg_func_buf;

String dbg_type( bool *p ){
	return "Bool";
}

String dbg_type( int *p ){
	return "Int";
}

String dbg_type( Float *p ){
	return "Float";
}

String dbg_type( String *p ){
	return "String";
}

template<class T> String dbg_type( T **p ){
	return "Object";
}

template<class T> String dbg_type( Array<T> *p ){
	return dbg_type( &(*p)[0] )+"[]";
}

String dbg_value( bool *p ){
	return *p ? "True" : "False";
}

String dbg_value( int *p ){
	return String( *p );
}

String dbg_value( Float *p ){
	return String( *p );
}

String dbg_value( String *p ){
	String t=*p;
	if( t.Length()>100 ) t=t.Slice( 0,100 )+"...";
	t=t.Replace( "\"","~q" );
	t=t.Replace( "\t","~t" );
	t=t.Replace( "\n","~n" );
	t=t.Replace( "\r","~r" );
	return String("\"")+t+"\"";
}

template<class T> String dbg_value( T **t ){
	Object *p=dynamic_cast<Object*>( *t );
	char buf[64];
	sprintf_s( buf,"%p",p );
	return String("@") + (buf[0]=='0' && buf[1]=='x' ? buf+2 : buf );
}

template<class T> String dbg_value( Array<T> *p ){
	String t="[";
	int n=(*p).Length();
	if( n>100 ) n=100;
	for( int i=0;i<n;++i ){
		if( i ) t+=",";
		t+=dbg_value( &(*p)[i] );
	}
	return t+"]";
}

String dbg_ptr_value( void *p ){
	char buf[64];
	sprintf_s( buf,"%p",p );
	return (buf[0]=='0' && buf[1]=='x' ? buf+2 : buf );
}

template<class T> String dbg_decl( const char *id,T *ptr ){
	return String( id )+":"+dbg_type(ptr)+"="+dbg_value(ptr)+"\n";
}

struct dbg_var_type{
	virtual String type( void *p )=0;
	virtual String value( void *p )=0;
};

template<class T> struct dbg_var_type_t : public dbg_var_type{

	String type( void *p ){
		return dbg_type( (T*)p );
	}
	
	String value( void *p ){
		return dbg_value( (T*)p );
	}
	
	static dbg_var_type_t<T> info;
};
template<class T> dbg_var_type_t<T> dbg_var_type_t<T>::info;

struct dbg_blk{
	void **var_ptr;
	
	dbg_blk():var_ptr(dbg_var_ptr){
		if( dbg_stepmode=='l' ) --dbg_suspend;
	}
	
	~dbg_blk(){
		if( dbg_stepmode=='l' ) ++dbg_suspend;
		dbg_var_ptr=var_ptr;
	}
};

struct dbg_func : public dbg_blk{
	const char *id;
	const char *info;

	dbg_func( const char *p ):id(p),info(dbg_info){
		*dbg_func_ptr++=this;
		if( dbg_stepmode=='s' ) --dbg_suspend;
	}
	
	~dbg_func(){
		if( dbg_stepmode=='s' ) ++dbg_suspend;
		--dbg_func_ptr;
		dbg_info=info;
	}
};

int dbg_print( String t ){
	static char *buf;
	static int len;
	int n=t.Length();
	if( n+100>len ){
		len=n+100;
		free( buf );
		buf=(char*)malloc( len );
	}
	buf[n]='\n';
	for( int i=0;i<n;++i ) buf[i]=t[i];
	fwrite( buf,n+1,1,dbg_stream );
	fflush( dbg_stream );
	return 0;
}

void dbg_callstack(){

	void **var_ptr=dbg_var_buf;
	dbg_func **func_ptr=dbg_func_buf;
	
	while( var_ptr!=dbg_var_ptr ){
		while( func_ptr!=dbg_func_ptr && var_ptr==(*func_ptr)->var_ptr ){
			const char *id=(*func_ptr++)->id;
			const char *info=func_ptr!=dbg_func_ptr ? (*func_ptr)->info : dbg_info;
			fprintf( dbg_stream,"+%s;%s\n",id,info );
		}
		void *vp=*var_ptr++;
		const char *nm=(const char*)*var_ptr++;
		dbg_var_type *ty=(dbg_var_type*)*var_ptr++;
		dbg_print( String(nm)+":"+ty->type(vp)+"="+ty->value(vp) );
	}
	while( func_ptr!=dbg_func_ptr ){
		const char *id=(*func_ptr++)->id;
		const char *info=func_ptr!=dbg_func_ptr ? (*func_ptr)->info : dbg_info;
		fprintf( dbg_stream,"+%s;%s\n",id,info );
	}
}

String dbg_stacktrace(){
	if( !dbg_info || !dbg_info[0] ) return "";
	String str=String( dbg_info )+"\n";
	dbg_func **func_ptr=dbg_func_ptr;
	if( func_ptr==dbg_func_buf ) return str;
	while( --func_ptr!=dbg_func_buf ){
		str+=String( (*func_ptr)->info )+"\n";
	}
	return str;
}

void dbg_throw( const char *err ){
	dbg_exstack=dbg_stacktrace();
	throw err;
}

void dbg_stop(){

#if TARGET_OS_IPHONE
	dbg_throw( "STOP" );
#endif

	fprintf( dbg_stream,"{{~~%s~~}}\n",dbg_info );
	dbg_callstack();
	dbg_print( "" );
	
	for(;;){

		char buf[256];
		char *e=fgets( buf,256,stdin );
		if( !e ) exit( -1 );
		
		e=strchr( buf,'\n' );
		if( !e ) exit( -1 );
		
		*e=0;
		
		Object *p;
		
		switch( buf[0] ){
		case '?':
			break;
		case 'r':	//run
			dbg_suspend=0;		
			dbg_stepmode=0;
			return;
		case 's':	//step
			dbg_suspend=1;
			dbg_stepmode='s';
			return;
		case 'e':	//enter func
			dbg_suspend=1;
			dbg_stepmode='e';
			return;
		case 'l':	//leave block
			dbg_suspend=0;
			dbg_stepmode='l';
			return;
		case '@':	//dump object
			p=0;
			sscanf_s( buf+1,"%p",&p );
			if( p ){
				dbg_print( p->debug() );
			}else{
				dbg_print( "" );
			}
			break;
		case 'q':	//quit!
			exit( 0 );
			break;			
		default:
			printf( "????? %s ?????",buf );fflush( stdout );
			exit( -1 );
		}
	}
}

void dbg_error( const char *err ){

#if TARGET_OS_IPHONE
	dbg_throw( err );
#endif

	for(;;){
		bbPrint( String("Monkey Runtime Error : ")+err );
		bbPrint( dbg_stacktrace() );
		dbg_stop();
	}
}

#define DBG_INFO(X) dbg_info=(X);if( dbg_suspend>0 ) dbg_stop();

#define DBG_ENTER(P) dbg_func _dbg_func(P);

#define DBG_BLOCK() dbg_blk _dbg_blk;

#define DBG_GLOBAL( ID,NAME )	//TODO!

#define DBG_LOCAL( ID,NAME )\
*dbg_var_ptr++=&ID;\
*dbg_var_ptr++=(void*)NAME;\
*dbg_var_ptr++=&dbg_var_type_t<dbg_typeof(ID)>::info;

//**** main ****

int argc;
const char **argv;

Float D2R=0.017453292519943295f;
Float R2D=57.29577951308232f;

int bbPrint( String t ){

	static std::vector<unsigned char> buf;
	buf.clear();
	t.Save( buf );
	buf.push_back( '\n' );
	buf.push_back( 0 );
	
#if __cplusplus_winrt	//winrt?

#if CFG_WINRT_PRINT_ENABLED
	OutputDebugStringA( (const char*)&buf[0] );
#endif

#elif _WIN32			//windows?

	fputs( (const char*)&buf[0],stdout );
	fflush( stdout );

#elif __APPLE__			//macos/ios?

	fputs( (const char*)&buf[0],stdout );
	fflush( stdout );
	
#elif __linux			//linux?

#if CFG_ANDROID_NDK_PRINT_ENABLED
	LOGI( (const char*)&buf[0] );
#else
	fputs( (const char*)&buf[0],stdout );
	fflush( stdout );
#endif

#endif

	return 0;
}

class BBExitApp{
};

int bbError( String err ){
	if( !err.Length() ){
#if __cplusplus_winrt
		throw BBExitApp();
#else
		exit( 0 );
#endif
	}
	dbg_error( err.ToCString<char>() );
	return 0;
}

int bbDebugLog( String t ){
	bbPrint( t );
	return 0;
}

int bbDebugStop(){
	dbg_stop();
	return 0;
}

int bbInit();
int bbMain();

#if _MSC_VER

static void _cdecl seTranslator( unsigned int ex,EXCEPTION_POINTERS *p ){

	switch( ex ){
	case EXCEPTION_ACCESS_VIOLATION:dbg_error( "Memory access violation" );
	case EXCEPTION_ILLEGAL_INSTRUCTION:dbg_error( "Illegal instruction" );
	case EXCEPTION_INT_DIVIDE_BY_ZERO:dbg_error( "Integer divide by zero" );
	case EXCEPTION_STACK_OVERFLOW:dbg_error( "Stack overflow" );
	}
	dbg_error( "Unknown exception" );
}

#else

void sighandler( int sig  ){
	switch( sig ){
	case SIGSEGV:dbg_error( "Memory access violation" );
	case SIGILL:dbg_error( "Illegal instruction" );
	case SIGFPE:dbg_error( "Floating point exception" );
#if !_WIN32
	case SIGBUS:dbg_error( "Bus error" );
#endif	
	}
	dbg_error( "Unknown signal" );
}

#endif

//entry point call by target main()...
//
int bb_std_main( int argc,const char **argv ){

	::argc=argc;
	::argv=argv;
	
#if _MSC_VER

	_set_se_translator( seTranslator );

#else
	
	signal( SIGSEGV,sighandler );
	signal( SIGILL,sighandler );
	signal( SIGFPE,sighandler );
#if !_WIN32
	signal( SIGBUS,sighandler );
#endif

#endif

	if( !setlocale( LC_CTYPE,"en_US.UTF-8" ) ){
		setlocale( LC_CTYPE,"" );
	}

	gc_init1();

	bbInit();
	
	gc_init2();

	bbMain();

	return 0;
}


//***** game.h *****

struct BBGameEvent{
	enum{
		None=0,
		KeyDown=1,KeyUp=2,KeyChar=3,
		MouseDown=4,MouseUp=5,MouseMove=6,
		TouchDown=7,TouchUp=8,TouchMove=9,
		MotionAccel=10
	};
};

class BBGameDelegate : public Object{
public:
	virtual void StartGame(){}
	virtual void SuspendGame(){}
	virtual void ResumeGame(){}
	virtual void UpdateGame(){}
	virtual void RenderGame(){}
	virtual void KeyEvent( int event,int data ){}
	virtual void MouseEvent( int event,int data,Float x,Float y ){}
	virtual void TouchEvent( int event,int data,Float x,Float y ){}
	virtual void MotionEvent( int event,int data,Float x,Float y,Float z ){}
	virtual void DiscardGraphics(){}
};

struct BBDisplayMode : public Object{
	int width;
	int height;
	int depth;
	int hertz;
	int flags;
	BBDisplayMode( int width=0,int height=0,int depth=0,int hertz=0,int flags=0 ):width(width),height(height),depth(depth),hertz(hertz),flags(flags){}
};

class BBGame{
public:
	BBGame();
	virtual ~BBGame(){}
	
	// ***** Extern *****
	static BBGame *Game(){ return _game; }
	
	virtual void SetDelegate( BBGameDelegate *delegate );
	virtual BBGameDelegate *Delegate(){ return _delegate; }
	
	virtual void SetKeyboardEnabled( bool enabled );
	virtual bool KeyboardEnabled();
	
	virtual void SetUpdateRate( int updateRate );
	virtual int UpdateRate();
	
	virtual bool Started(){ return _started; }
	virtual bool Suspended(){ return _suspended; }
	
	virtual int Millisecs();
	virtual void GetDate( Array<int> date );
	virtual int SaveState( String state );
	virtual String LoadState();
	virtual String LoadString( String path );
	virtual bool PollJoystick( int port,Array<Float> joyx,Array<Float> joyy,Array<Float> joyz,Array<bool> buttons );
	virtual void OpenUrl( String url );
	virtual void SetMouseVisible( bool visible );
	
	virtual int GetDeviceWidth(){ return 0; }
	virtual int GetDeviceHeight(){ return 0; }
	virtual void SetDeviceWindow( int width,int height,int flags ){}
	virtual Array<BBDisplayMode*> GetDisplayModes(){ return Array<BBDisplayMode*>(); }
	virtual BBDisplayMode *GetDesktopMode(){ return 0; }
	virtual void SetSwapInterval( int interval ){}

	// ***** Native *****
	virtual String PathToFilePath( String path );
	virtual FILE *OpenFile( String path,String mode );
	virtual unsigned char *LoadData( String path,int *plength );
	virtual unsigned char *LoadImageData( String path,int *width,int *height,int *depth ){ return 0; }
	virtual unsigned char *LoadAudioData( String path,int *length,int *channels,int *format,int *hertz ){ return 0; }
	
	//***** Internal *****
	virtual void Die( ThrowableObject *ex );
	virtual void gc_collect();
	virtual void StartGame();
	virtual void SuspendGame();
	virtual void ResumeGame();
	virtual void UpdateGame();
	virtual void RenderGame();
	virtual void KeyEvent( int ev,int data );
	virtual void MouseEvent( int ev,int data,float x,float y );
	virtual void TouchEvent( int ev,int data,float x,float y );
	virtual void MotionEvent( int ev,int data,float x,float y,float z );
	virtual void DiscardGraphics();
	
protected:

	static BBGame *_game;

	BBGameDelegate *_delegate;
	bool _keyboardEnabled;
	int _updateRate;
	bool _started;
	bool _suspended;
};

//***** game.cpp *****

BBGame *BBGame::_game;

BBGame::BBGame():
_delegate( 0 ),
_keyboardEnabled( false ),
_updateRate( 0 ),
_started( false ),
_suspended( false ){
	_game=this;
}

void BBGame::SetDelegate( BBGameDelegate *delegate ){
	_delegate=delegate;
}

void BBGame::SetKeyboardEnabled( bool enabled ){
	_keyboardEnabled=enabled;
}

bool BBGame::KeyboardEnabled(){
	return _keyboardEnabled;
}

void BBGame::SetUpdateRate( int updateRate ){
	_updateRate=updateRate;
}

int BBGame::UpdateRate(){
	return _updateRate;
}

int BBGame::Millisecs(){
	return 0;
}

void BBGame::GetDate( Array<int> date ){
	int n=date.Length();
	if( n>0 ){
		time_t t=time( 0 );
		
#if _MSC_VER
		struct tm tii;
		struct tm *ti=&tii;
		localtime_s( ti,&t );
#else
		struct tm *ti=localtime( &t );
#endif

		date[0]=ti->tm_year+1900;
		if( n>1 ){ 
			date[1]=ti->tm_mon+1;
			if( n>2 ){
				date[2]=ti->tm_mday;
				if( n>3 ){
					date[3]=ti->tm_hour;
					if( n>4 ){
						date[4]=ti->tm_min;
						if( n>5 ){
							date[5]=ti->tm_sec;
							if( n>6 ){
								date[6]=0;
							}
						}
					}
				}
			}
		}
	}
}

int BBGame::SaveState( String state ){
	if( FILE *f=OpenFile( "./.monkeystate","wb" ) ){
		bool ok=state.Save( f );
		fclose( f );
		return ok ? 0 : -2;
	}
	return -1;
}

String BBGame::LoadState(){
	if( FILE *f=OpenFile( "./.monkeystate","rb" ) ){
		String str=String::Load( f );
		fclose( f );
		return str;
	}
	return "";
}

String BBGame::LoadString( String path ){
	if( FILE *fp=OpenFile( path,"rb" ) ){
		String str=String::Load( fp );
		fclose( fp );
		return str;
	}
	return "";
}

bool BBGame::PollJoystick( int port,Array<Float> joyx,Array<Float> joyy,Array<Float> joyz,Array<bool> buttons ){
	return false;
}

void BBGame::OpenUrl( String url ){
}

void BBGame::SetMouseVisible( bool visible ){
}

//***** C++ Game *****

String BBGame::PathToFilePath( String path ){
	return path;
}

FILE *BBGame::OpenFile( String path,String mode ){
	path=PathToFilePath( path );
	if( path=="" ) return 0;
	
#if __cplusplus_winrt
	path=path.Replace( "/","\\" );
	FILE *f;
	if( _wfopen_s( &f,path.ToCString<wchar_t>(),mode.ToCString<wchar_t>() ) ) return 0;
	return f;
#elif _WIN32
	return _wfopen( path.ToCString<wchar_t>(),mode.ToCString<wchar_t>() );
#else
	return fopen( path.ToCString<char>(),mode.ToCString<char>() );
#endif
}

unsigned char *BBGame::LoadData( String path,int *plength ){

	FILE *f=OpenFile( path,"rb" );
	if( !f ) return 0;

	const int BUF_SZ=4096;
	std::vector<void*> tmps;
	int length=0;
	
	for(;;){
		void *p=malloc( BUF_SZ );
		int n=fread( p,1,BUF_SZ,f );
		tmps.push_back( p );
		length+=n;
		if( n!=BUF_SZ ) break;
	}
	fclose( f );
	
	unsigned char *data=(unsigned char*)malloc( length );
	unsigned char *p=data;
	
	int sz=length;
	for( int i=0;i<tmps.size();++i ){
		int n=sz>BUF_SZ ? BUF_SZ : sz;
		memcpy( p,tmps[i],n );
		free( tmps[i] );
		sz-=n;
		p+=n;
	}
	
	*plength=length;
	
	gc_ext_malloced( length );
	
	return data;
}

//***** INTERNAL *****

void BBGame::Die( ThrowableObject *ex ){
	bbPrint( "Monkey Runtime Error : Uncaught Monkey Exception" );
#ifndef NDEBUG
	bbPrint( ex->stackTrace );
#endif
	exit( -1 );
}

void BBGame::gc_collect(){
	gc_mark( _delegate );
	::gc_collect();
}

void BBGame::StartGame(){

	if( _started ) return;
	_started=true;
	
	try{
		_delegate->StartGame();
	}catch( ThrowableObject *ex ){
		Die( ex );
	}
	gc_collect();
}

void BBGame::SuspendGame(){

	if( !_started || _suspended ) return;
	_suspended=true;
	
	try{
		_delegate->SuspendGame();
	}catch( ThrowableObject *ex ){
		Die( ex );
	}
	gc_collect();
}

void BBGame::ResumeGame(){

	if( !_started || !_suspended ) return;
	_suspended=false;
	
	try{
		_delegate->ResumeGame();
	}catch( ThrowableObject *ex ){
		Die( ex );
	}
	gc_collect();
}

void BBGame::UpdateGame(){

	if( !_started || _suspended ) return;
	
	try{
		_delegate->UpdateGame();
	}catch( ThrowableObject *ex ){
		Die( ex );
	}
	gc_collect();
}

void BBGame::RenderGame(){

	if( !_started ) return;
	
	try{
		_delegate->RenderGame();
	}catch( ThrowableObject *ex ){
		Die( ex );
	}
	gc_collect();
}

void BBGame::KeyEvent( int ev,int data ){

	if( !_started ) return;
	
	try{
		_delegate->KeyEvent( ev,data );
	}catch( ThrowableObject *ex ){
		Die( ex );
	}
	gc_collect();
}

void BBGame::MouseEvent( int ev,int data,float x,float y ){

	if( !_started ) return;
	
	try{
		_delegate->MouseEvent( ev,data,x,y );
	}catch( ThrowableObject *ex ){
		Die( ex );
	}
	gc_collect();
}

void BBGame::TouchEvent( int ev,int data,float x,float y ){

	if( !_started ) return;
	
	try{
		_delegate->TouchEvent( ev,data,x,y );
	}catch( ThrowableObject *ex ){
		Die( ex );
	}
	gc_collect();
}

void BBGame::MotionEvent( int ev,int data,float x,float y,float z ){

	if( !_started ) return;
	
	try{
		_delegate->MotionEvent( ev,data,x,y,z );
	}catch( ThrowableObject *ex ){
		Die( ex );
	}
	gc_collect();
}

void BBGame::DiscardGraphics(){

	if( !_started ) return;
	
	try{
		_delegate->DiscardGraphics();
	}catch( ThrowableObject *ex ){
		Die( ex );
	}
	gc_collect();
}


//***** wavloader.h *****
//
unsigned char *LoadWAV( FILE *f,int *length,int *channels,int *format,int *hertz );

//***** wavloader.cpp *****
//
static const char *readTag( FILE *f ){
	static char buf[8];
	if( fread( buf,4,1,f )!=1 ) return "";
	buf[4]=0;
	return buf;
}

static int readInt( FILE *f ){
	unsigned char buf[4];
	if( fread( buf,4,1,f )!=1 ) return -1;
	return (buf[3]<<24) | (buf[2]<<16) | (buf[1]<<8) | buf[0];
}

static int readShort( FILE *f ){
	unsigned char buf[2];
	if( fread( buf,2,1,f )!=1 ) return -1;
	return (buf[1]<<8) | buf[0];
}

static void skipBytes( int n,FILE *f ){
	char *p=(char*)malloc( n );
	fread( p,n,1,f );
	free( p );
}

unsigned char *LoadWAV( FILE *f,int *plength,int *pchannels,int *pformat,int *phertz ){
	if( !strcmp( readTag( f ),"RIFF" ) ){
		int len=readInt( f )-8;len=len;
		if( !strcmp( readTag( f ),"WAVE" ) ){
			if( !strcmp( readTag( f ),"fmt " ) ){
				int len2=readInt( f );
				int comp=readShort( f );
				if( comp==1 ){
					int chans=readShort( f );
					int hertz=readInt( f );
					int bytespersec=readInt( f );bytespersec=bytespersec;
					int pad=readShort( f );pad=pad;
					int bits=readShort( f );
					int format=bits/8;
					if( len2>16 ) skipBytes( len2-16,f );
					for(;;){
						const char *p=readTag( f );
						if( feof( f ) ) break;
						int size=readInt( f );
						if( strcmp( p,"data" ) ){
							skipBytes( size,f );
							continue;
						}
						unsigned char *data=(unsigned char*)malloc( size );
						if( fread( data,size,1,f )==1 ){
							*plength=size/(chans*format);
							*pchannels=chans;
							*pformat=format;
							*phertz=hertz;
							return data;
						}
						free( data );
					}
				}
			}
		}
	}
	return 0;
}



//***** oggloader.h *****
unsigned char *LoadOGG( FILE *f,int *length,int *channels,int *format,int *hertz );

//***** oggloader.cpp *****
unsigned char *LoadOGG( FILE *f,int *length,int *channels,int *format,int *hertz ){

	int error;
	stb_vorbis *v=stb_vorbis_open_file( f,0,&error,0 );
	if( !v ) return 0;
	
	stb_vorbis_info info=stb_vorbis_get_info( v );
	
	int limit=info.channels*4096;
	int offset=0,total=limit;

	short *data=(short*)malloc( total*2 );
	
	for(;;){
		int n=stb_vorbis_get_frame_short_interleaved( v,info.channels,data+offset,total-offset );
		if( !n ) break;
	
		offset+=n*info.channels;
		
		if( offset+limit>total ){
			total*=2;
			data=(short*)realloc( data,total*2 );
		}
	}
	
	data=(short*)realloc( data,offset*2 );
	
	*length=offset/info.channels;
	*channels=info.channels;
	*format=2;
	*hertz=info.sample_rate;
	
	stb_vorbis_close(v);
	
	return (unsigned char*)data;
}



//***** glfwgame.h *****

struct BBGlfwVideoMode : public Object{
	int Width;
	int Height;
	int RedBits;
	int GreenBits;
	int BlueBits;
	BBGlfwVideoMode( int w,int h,int r,int g,int b ):Width(w),Height(h),RedBits(r),GreenBits(g),BlueBits(b){}
};

class BBGlfwGame : public BBGame{
public:
	BBGlfwGame();

	static BBGlfwGame *GlfwGame(){ return _glfwGame; }
	
	virtual void SetUpdateRate( int hertz );
	virtual int Millisecs();
	virtual bool PollJoystick( int port,Array<Float> joyx,Array<Float> joyy,Array<Float> joyz,Array<bool> buttons );
	virtual void OpenUrl( String url );
	virtual void SetMouseVisible( bool visible );
	
	virtual int GetDeviceWidth();
	virtual int GetDeviceHeight();
	virtual void SetDeviceWindow( int width,int height,int flags );
	virtual Array<BBDisplayMode*> GetDisplayModes();
	virtual BBDisplayMode *GetDesktopMode();
	virtual void SetSwapInterval( int interval );

	virtual int SaveState( String state );
	virtual String LoadState();
	virtual String PathToFilePath( String path );
	virtual unsigned char *LoadImageData( String path,int *width,int *height,int *depth );
	virtual unsigned char *LoadAudioData( String path,int *length,int *channels,int *format,int *hertz );
	
	virtual void SetGlfwWindow( int width,int height,int red,int green,int blue,int alpha,int depth,int stencil,bool fullscreen );
	virtual BBGlfwVideoMode *GetGlfwDesktopMode();
	virtual Array<BBGlfwVideoMode*> GetGlfwVideoModes();
	
	virtual void Run();
	
private:
	static BBGlfwGame *_glfwGame;

	double _updatePeriod;
	double _nextUpdate;
	
	String _baseDir;
	String _internalDir;
	
	int _swapInterval;

	void UpdateEvents();
		
protected:
	static int TransKey( int key );
	static int KeyToChar( int key );
	
	static void GLFWCALL OnKey( int key,int action );
	static void GLFWCALL OnChar( int chr,int action );
	static void GLFWCALL OnMouseButton( int button,int action );
	static void GLFWCALL OnMousePos( int x,int y );
	static int  GLFWCALL OnWindowClose();
};

//***** glfwgame.cpp *****

enum{
	VKEY_BACKSPACE=8,VKEY_TAB,
	VKEY_ENTER=13,
	VKEY_SHIFT=16,
	VKEY_CONTROL=17,
	VKEY_ESC=27,
	VKEY_SPACE=32,
	VKEY_PAGEUP=33,VKEY_PAGEDOWN,VKEY_END,VKEY_HOME,
	VKEY_LEFT=37,VKEY_UP,VKEY_RIGHT,VKEY_DOWN,
	VKEY_INSERT=45,VKEY_DELETE,
	VKEY_0=48,VKEY_1,VKEY_2,VKEY_3,VKEY_4,VKEY_5,VKEY_6,VKEY_7,VKEY_8,VKEY_9,
	VKEY_A=65,VKEY_B,VKEY_C,VKEY_D,VKEY_E,VKEY_F,VKEY_G,VKEY_H,VKEY_I,VKEY_J,
	VKEY_K,VKEY_L,VKEY_M,VKEY_N,VKEY_O,VKEY_P,VKEY_Q,VKEY_R,VKEY_S,VKEY_T,
	VKEY_U,VKEY_V,VKEY_W,VKEY_X,VKEY_Y,VKEY_Z,
	
	VKEY_LSYS=91,VKEY_RSYS,
	
	VKEY_NUM0=96,VKEY_NUM1,VKEY_NUM2,VKEY_NUM3,VKEY_NUM4,
	VKEY_NUM5,VKEY_NUM6,VKEY_NUM7,VKEY_NUM8,VKEY_NUM9,
	VKEY_NUMMULTIPLY=106,VKEY_NUMADD,VKEY_NUMSLASH,
	VKEY_NUMSUBTRACT,VKEY_NUMDECIMAL,VKEY_NUMDIVIDE,

	VKEY_F1=112,VKEY_F2,VKEY_F3,VKEY_F4,VKEY_F5,VKEY_F6,
	VKEY_F7,VKEY_F8,VKEY_F9,VKEY_F10,VKEY_F11,VKEY_F12,

	VKEY_LSHIFT=160,VKEY_RSHIFT,
	VKEY_LCONTROL=162,VKEY_RCONTROL,
	VKEY_LALT=164,VKEY_RALT,

	VKEY_TILDE=192,VKEY_MINUS=189,VKEY_EQUALS=187,
	VKEY_OPENBRACKET=219,VKEY_BACKSLASH=220,VKEY_CLOSEBRACKET=221,
	VKEY_SEMICOLON=186,VKEY_QUOTES=222,
	VKEY_COMMA=188,VKEY_PERIOD=190,VKEY_SLASH=191
};

enum{
	JOY_A=0x00,
	JOY_B=0x01,
	JOY_X=0x02,
	JOY_Y=0x03,
	JOY_LB=0x04,
	JOY_RB=0x05,
	JOY_BACK=0x06,
	JOY_START=0x07,
	JOY_LEFT=0x08,
	JOY_UP=0x09,
	JOY_RIGHT=0x0a,
	JOY_DOWN=0x0b,
	JOY_LSB=0x0c,
	JOY_RSB=0x0d,
	JOY_MENU=0x0e
};

BBGlfwGame *BBGlfwGame::_glfwGame;

BBGlfwGame::BBGlfwGame():_updatePeriod(0),_nextUpdate(0),_swapInterval( CFG_GLFW_SWAP_INTERVAL ){
	_glfwGame=this;
}

//***** BBGame *****

void Init_GL_Exts();

int glfwGraphicsSeq=0;

void BBGlfwGame::SetUpdateRate( int updateRate ){
	BBGame::SetUpdateRate( updateRate );
	if( _updateRate ) _updatePeriod=1.0/_updateRate;
	_nextUpdate=0;
}

int BBGlfwGame::Millisecs(){
	return int( glfwGetTime()*1000.0 );
}

bool BBGlfwGame::PollJoystick( int port,Array<Float> joyx,Array<Float> joyy,Array<Float> joyz,Array<bool> buttons ){

	//Just in case...my PC has either started doing weird things with joystick ordering, or I assumed too much in the past!
	static int pjoys[4];
	if( !port ){
		int i=0;
		for( int joy=GLFW_JOYSTICK_1;joy<=GLFW_JOYSTICK_16 && i<4;++joy ){
			if( glfwGetJoystickParam( joy,GLFW_PRESENT ) ) pjoys[i++]=joy;
		}
		while( i<4 ) pjoys[i++]=-1;
	}
	int joy=pjoys[port];
	if( joy==-1 ) return false;
	
	//Stopped working on my PC at some point...
//	int joy=GLFW_JOYSTICK_1+port;
//	if( !glfwGetJoystickParam( joy,GLFW_PRESENT ) ) return false;

	//read axes
	float axes[6];
	memset( axes,0,sizeof(axes) );
	int n_axes=glfwGetJoystickPos( joy,axes,6 );
	
	//read buttons
	unsigned char buts[32];
	memset( buts,0,sizeof(buts) );
	int n_buts=glfwGetJoystickButtons( joy,buts,32 );

//	static int done;
//	if( !done++ ) printf( "n_axes=%i, n_buts=%i\n",n_axes,n_buts );fflush( stdout );

	//Ugh...
	
	const int *dev_axes;
	const int *dev_buttons;
	
#if _WIN32
	
	//xbox 360 controller
	const int xbox360_axes[]={0,1,2,4,0x43,0x42,999};
	const int xbox360_buttons[]={0,1,2,3,4,5,6,7,-4,-3,-2,-1,8,9,999};
	
	//logitech dual action
	const int logitech_axes[]={0,1,0x86,2,0x43,0x87,999};
	const int logitech_buttons[]={1,2,0,3,4,5,8,9,-4,-3,-2,-1,10,11,999};
	
	if( n_axes==5 && n_buts==14 ){
		dev_axes=xbox360_axes;
		dev_buttons=xbox360_buttons;
	}else{
		dev_axes=logitech_axes;
		dev_buttons=logitech_buttons;
	}
	
#else

	//xbox 360 controller
	const int xbox360_axes[]={0,1,0x14,2,3,0x25,999};
	const int xbox360_buttons[]={11,12,13,14,8,9,5,4,2,0,3,1,6,7,10,999};

	//ps3 controller
	const int ps3_axes[]={0,1,0x88,2,3,0x89,999};
	const int ps3_buttons[]={14,13,15,12,10,11,0,3,7,4,5,6,1,2,16,999};

	//logitech dual action
	const int logitech_axes[]={0,1,0x86,2,3,0x87,999};
	const int logitech_buttons[]={1,2,0,3,4,5,8,9,15,12,13,14,10,11,999};

	if( n_axes==6 && n_buts==15 ){
		dev_axes=xbox360_axes;
		dev_buttons=xbox360_buttons;
	}else if( n_axes==4 && n_buts==19 ){
		dev_axes=ps3_axes;
		dev_buttons=ps3_buttons;
	}else{
		dev_axes=logitech_axes;
		dev_buttons=logitech_buttons;
	}

#endif

	const int *p=dev_axes;
	
	float joys[6]={0,0,0,0,0,0};
	
	for( int i=0;i<6 && p[i]!=999;++i ){
		int j=p[i]&0xf,k=p[i]&~0xf;
		if( k==0x10 ){
			joys[i]=(axes[j]+1)/2;
		}else if( k==0x20 ){
			joys[i]=(1-axes[j])/2;
		}else if( k==0x40 ){
			joys[i]=-axes[j];
		}else if( k==0x80 ){
			joys[i]=(buts[j]==GLFW_PRESS);
		}else{
			joys[i]=axes[j];
		}
	}
	
	joyx[0]=joys[0];joyy[0]=joys[1];joyz[0]=joys[2];
	joyx[1]=joys[3];joyy[1]=joys[4];joyz[1]=joys[5];
	
	p=dev_buttons;
	
	for( int i=0;i<32;++i ) buttons[i]=false;
	
	for( int i=0;i<32 && p[i]!=999;++i ){
		int j=p[i];
		if( j<0 ) j+=n_buts;
		buttons[i]=(buts[j]==GLFW_PRESS);
	}

	return true;
}

void BBGlfwGame::OpenUrl( String url ){
#if _WIN32
	ShellExecute( HWND_DESKTOP,"open",url.ToCString<char>(),0,0,SW_SHOWNORMAL );
#elif __APPLE__
	if( CFURLRef cfurl=CFURLCreateWithBytes( 0,url.ToCString<UInt8>(),url.Length(),kCFStringEncodingASCII,0 ) ){
		LSOpenCFURLRef( cfurl,0 );
		CFRelease( cfurl );
	}
#elif __linux
	system( ( String( "xdg-open \"" )+url+"\"" ).ToCString<char>() );
#endif
}

void BBGlfwGame::SetMouseVisible( bool visible ){
	if( visible ){
		glfwEnable( GLFW_MOUSE_CURSOR );
	}else{
		glfwDisable( GLFW_MOUSE_CURSOR );
	}
}

int BBGlfwGame::SaveState( String state ){
#ifdef CFG_GLFW_APP_LABEL
	if( FILE *f=OpenFile( "monkey://internal/.monkeystate","wb" ) ){
		bool ok=state.Save( f );
		fclose( f );
		return ok ? 0 : -2;
	}
	return -1;
#else
	return BBGame::SaveState( state );
#endif
}

String BBGlfwGame::LoadState(){
#ifdef CFG_GLFW_APP_LABEL
	if( FILE *f=OpenFile( "monkey://internal/.monkeystate","rb" ) ){
		String str=String::Load( f );
		fclose( f );
		return str;
	}
	return "";
#else
	return BBGame::LoadState();
#endif
}

String BBGlfwGame::PathToFilePath( String path ){

	if( !_baseDir.Length() ){
	
		String appPath;

#if _WIN32
		WCHAR buf[MAX_PATH+1];
		GetModuleFileNameW( GetModuleHandleW(0),buf,MAX_PATH );
		buf[MAX_PATH]=0;appPath=String( buf ).Replace( "\\","/" );

#elif __APPLE__

		char buf[PATH_MAX+1];
		uint32_t size=sizeof( buf );
		_NSGetExecutablePath( buf,&size );
		buf[PATH_MAX]=0;appPath=String( buf ).Replace( "/./","/" );
	
#elif __linux
		char lnk[PATH_MAX+1],buf[PATH_MAX];
		sprintf( lnk,"/proc/%i/exe",getpid() );
		int n=readlink( lnk,buf,PATH_MAX );
		if( n<0 || n>=PATH_MAX ) abort();
		appPath=String( buf,n );

#endif
		int i=appPath.FindLast( "/" );if( i==-1 ) abort();
		_baseDir=appPath.Slice( 0,i );
		
#if __APPLE__
		if( _baseDir.EndsWith( ".app/Contents/MacOS" ) ) _baseDir=_baseDir.Slice( 0,-5 )+"Resources";
#endif
//		bbPrint( String( "_baseDir=" )+_baseDir );
	}
	
	if( !path.StartsWith( "monkey:" ) ){
		return path;
	}else if( path.StartsWith( "monkey://data/" ) ){
		return _baseDir+"/data/"+path.Slice( 14 );
	}else if( path.StartsWith( "monkey://internal/" ) ){
		if( !_internalDir.Length() ){
#ifdef CFG_GLFW_APP_LABEL

#if _WIN32
			_internalDir=String( getenv( "APPDATA" ) );
#elif __APPLE__
			_internalDir=String( getenv( "HOME" ) )+"/Library/Application Support";
#elif __linux
			_internalDir=String( getenv( "HOME" ) )+"/.config";
			mkdir( _internalDir.ToCString<char>(),0777 );
#endif

#ifdef CFG_GLFW_APP_PUBLISHER
			_internalDir=_internalDir+"/"+_STRINGIZE( CFG_GLFW_APP_PUBLISHER );
#if _WIN32
			_wmkdir( _internalDir.ToCString<wchar_t>() );
#else
			mkdir( _internalDir.ToCString<char>(),0777 );
#endif
#endif

			_internalDir=_internalDir+"/"+_STRINGIZE( CFG_GLFW_APP_LABEL );
#if _WIN32
			_wmkdir( _internalDir.ToCString<wchar_t>() );
#else
			mkdir( _internalDir.ToCString<char>(),0777 );
#endif

#else
			_internalDir=_baseDir+"/internal";
#endif			
//			bbPrint( String( "_internalDir=" )+_internalDir );
		}
		return _internalDir+"/"+path.Slice( 18 );
	}else if( path.StartsWith( "monkey://external/" ) ){
		return _baseDir+"/external/"+path.Slice( 18 );
	}
	return "";
}

unsigned char *BBGlfwGame::LoadImageData( String path,int *width,int *height,int *depth ){

	FILE *f=OpenFile( path,"rb" );
	if( !f ) return 0;
	
	unsigned char *data=stbi_load_from_file( f,width,height,depth,0 );
	fclose( f );
	
	if( data ) gc_ext_malloced( (*width)*(*height)*(*depth) );
	
	return data;
}

unsigned char *BBGlfwGame::LoadAudioData( String path,int *length,int *channels,int *format,int *hertz ){

	FILE *f=OpenFile( path,"rb" );
	if( !f ) return 0;
	
	unsigned char *data=0;
	
	if( path.ToLower().EndsWith( ".wav" ) ){
		data=LoadWAV( f,length,channels,format,hertz );
	}else if( path.ToLower().EndsWith( ".ogg" ) ){
		data=LoadOGG( f,length,channels,format,hertz );
	}
	fclose( f );
	
	if( data ) gc_ext_malloced( (*length)*(*channels)*(*format) );
	
	return data;
}

//glfw key to monkey key!
int BBGlfwGame::TransKey( int key ){

	if( key>='0' && key<='9' ) return key;
	if( key>='A' && key<='Z' ) return key;

	switch( key ){

	case ' ':return VKEY_SPACE;
	case ';':return VKEY_SEMICOLON;
	case '=':return VKEY_EQUALS;
	case ',':return VKEY_COMMA;
	case '-':return VKEY_MINUS;
	case '.':return VKEY_PERIOD;
	case '/':return VKEY_SLASH;
	case '~':return VKEY_TILDE;
	case '[':return VKEY_OPENBRACKET;
	case ']':return VKEY_CLOSEBRACKET;
	case '\"':return VKEY_QUOTES;
	case '\\':return VKEY_BACKSLASH;
	
	case '`':return VKEY_TILDE;
	case '\'':return VKEY_QUOTES;

	case GLFW_KEY_LSHIFT:
	case GLFW_KEY_RSHIFT:return VKEY_SHIFT;
	case GLFW_KEY_LCTRL:
	case GLFW_KEY_RCTRL:return VKEY_CONTROL;
	
//	case GLFW_KEY_LSHIFT:return VKEY_LSHIFT;
//	case GLFW_KEY_RSHIFT:return VKEY_RSHIFT;
//	case GLFW_KEY_LCTRL:return VKEY_LCONTROL;
//	case GLFW_KEY_RCTRL:return VKEY_RCONTROL;
	
	case GLFW_KEY_BACKSPACE:return VKEY_BACKSPACE;
	case GLFW_KEY_TAB:return VKEY_TAB;
	case GLFW_KEY_ENTER:return VKEY_ENTER;
	case GLFW_KEY_ESC:return VKEY_ESC;
	case GLFW_KEY_INSERT:return VKEY_INSERT;
	case GLFW_KEY_DEL:return VKEY_DELETE;
	case GLFW_KEY_PAGEUP:return VKEY_PAGEUP;
	case GLFW_KEY_PAGEDOWN:return VKEY_PAGEDOWN;
	case GLFW_KEY_HOME:return VKEY_HOME;
	case GLFW_KEY_END:return VKEY_END;
	case GLFW_KEY_UP:return VKEY_UP;
	case GLFW_KEY_DOWN:return VKEY_DOWN;
	case GLFW_KEY_LEFT:return VKEY_LEFT;
	case GLFW_KEY_RIGHT:return VKEY_RIGHT;
	
	case GLFW_KEY_KP_0:return VKEY_NUM0;
	case GLFW_KEY_KP_1:return VKEY_NUM1;
	case GLFW_KEY_KP_2:return VKEY_NUM2;
	case GLFW_KEY_KP_3:return VKEY_NUM3;
	case GLFW_KEY_KP_4:return VKEY_NUM4;
	case GLFW_KEY_KP_5:return VKEY_NUM5;
	case GLFW_KEY_KP_6:return VKEY_NUM6;
	case GLFW_KEY_KP_7:return VKEY_NUM7;
	case GLFW_KEY_KP_8:return VKEY_NUM8;
	case GLFW_KEY_KP_9:return VKEY_NUM9;
	case GLFW_KEY_KP_DIVIDE:return VKEY_NUMDIVIDE;
	case GLFW_KEY_KP_MULTIPLY:return VKEY_NUMMULTIPLY;
	case GLFW_KEY_KP_SUBTRACT:return VKEY_NUMSUBTRACT;
	case GLFW_KEY_KP_ADD:return VKEY_NUMADD;
	case GLFW_KEY_KP_DECIMAL:return VKEY_NUMDECIMAL;
    	
	case GLFW_KEY_F1:return VKEY_F1;
	case GLFW_KEY_F2:return VKEY_F2;
	case GLFW_KEY_F3:return VKEY_F3;
	case GLFW_KEY_F4:return VKEY_F4;
	case GLFW_KEY_F5:return VKEY_F5;
	case GLFW_KEY_F6:return VKEY_F6;
	case GLFW_KEY_F7:return VKEY_F7;
	case GLFW_KEY_F8:return VKEY_F8;
	case GLFW_KEY_F9:return VKEY_F9;
	case GLFW_KEY_F10:return VKEY_F10;
	case GLFW_KEY_F11:return VKEY_F11;
	case GLFW_KEY_F12:return VKEY_F12;
	}
	return 0;
}

//monkey key to special monkey char
int BBGlfwGame::KeyToChar( int key ){
	switch( key ){
	case VKEY_BACKSPACE:
	case VKEY_TAB:
	case VKEY_ENTER:
	case VKEY_ESC:
		return key;
	case VKEY_PAGEUP:
	case VKEY_PAGEDOWN:
	case VKEY_END:
	case VKEY_HOME:
	case VKEY_LEFT:
	case VKEY_UP:
	case VKEY_RIGHT:
	case VKEY_DOWN:
	case VKEY_INSERT:
		return key | 0x10000;
	case VKEY_DELETE:
		return 127;
	}
	return 0;
}

void BBGlfwGame::OnMouseButton( int button,int action ){
	switch( button ){
	case GLFW_MOUSE_BUTTON_LEFT:button=0;break;
	case GLFW_MOUSE_BUTTON_RIGHT:button=1;break;
	case GLFW_MOUSE_BUTTON_MIDDLE:button=2;break;
	default:return;
	}
	int x,y;
	glfwGetMousePos( &x,&y );
	switch( action ){
	case GLFW_PRESS:
		_glfwGame->MouseEvent( BBGameEvent::MouseDown,button,x,y );
		break;
	case GLFW_RELEASE:
		_glfwGame->MouseEvent( BBGameEvent::MouseUp,button,x,y );
		break;
	}
}

void BBGlfwGame::OnMousePos( int x,int y ){
	_game->MouseEvent( BBGameEvent::MouseMove,-1,x,y );
}

int BBGlfwGame::OnWindowClose(){
	_game->KeyEvent( BBGameEvent::KeyDown,0x1b0 );
	_game->KeyEvent( BBGameEvent::KeyUp,0x1b0 );
	return GL_FALSE;
}

void BBGlfwGame::OnKey( int key,int action ){

	key=TransKey( key );
	if( !key ) return;
	
	switch( action ){
	case GLFW_PRESS:
		_glfwGame->KeyEvent( BBGameEvent::KeyDown,key );
		if( int chr=KeyToChar( key ) ) _game->KeyEvent( BBGameEvent::KeyChar,chr );
		break;
	case GLFW_RELEASE:
		_glfwGame->KeyEvent( BBGameEvent::KeyUp,key );
		break;
	}
}

void BBGlfwGame::OnChar( int chr,int action ){

	switch( action ){
	case GLFW_PRESS:
		_glfwGame->KeyEvent( BBGameEvent::KeyChar,chr );
		break;
	}
}

void BBGlfwGame::SetGlfwWindow( int width,int height,int red,int green,int blue,int alpha,int depth,int stencil,bool fullscreen ){

	for( int i=0;i<=GLFW_KEY_LAST;++i ){
		int key=TransKey( i );
		if( key && glfwGetKey( i )==GLFW_PRESS ) KeyEvent( BBGameEvent::KeyUp,key );
	}

	GLFWvidmode desktopMode;
	glfwGetDesktopMode( &desktopMode );

	glfwCloseWindow();
	
	glfwOpenWindowHint( GLFW_REFRESH_RATE,60 );
	glfwOpenWindowHint( GLFW_WINDOW_NO_RESIZE,CFG_GLFW_WINDOW_RESIZABLE ? GL_FALSE : GL_TRUE );

	glfwOpenWindow( width,height,red,green,blue,alpha,depth,stencil,fullscreen ? GLFW_FULLSCREEN : GLFW_WINDOW );

	++glfwGraphicsSeq;

	if( !fullscreen ){	
		glfwSetWindowPos( (desktopMode.Width-width)/2,(desktopMode.Height-height)/2 );
		glfwSetWindowTitle( _STRINGIZE(CFG_GLFW_WINDOW_TITLE) );
	}

#if CFG_OPENGL_INIT_EXTENSIONS
	Init_GL_Exts();
#endif

	if( _swapInterval>=0 ) glfwSwapInterval( _swapInterval );

	glfwEnable( GLFW_KEY_REPEAT );
	glfwDisable( GLFW_AUTO_POLL_EVENTS );
	glfwSetKeyCallback( OnKey );
	glfwSetCharCallback( OnChar );
	glfwSetMouseButtonCallback( OnMouseButton );
	glfwSetMousePosCallback( OnMousePos );
	glfwSetWindowCloseCallback(	OnWindowClose );
}

Array<BBGlfwVideoMode*> BBGlfwGame::GetGlfwVideoModes(){
	GLFWvidmode modes[1024];
	int n=glfwGetVideoModes( modes,1024 );
	Array<BBGlfwVideoMode*> bbmodes( n );
	for( int i=0;i<n;++i ){
		bbmodes[i]=new BBGlfwVideoMode( modes[i].Width,modes[i].Height,modes[i].RedBits,modes[i].GreenBits,modes[i].BlueBits );
	}
	return bbmodes;
}

BBGlfwVideoMode *BBGlfwGame::GetGlfwDesktopMode(){
	GLFWvidmode mode;
	glfwGetDesktopMode( &mode );
	return new BBGlfwVideoMode( mode.Width,mode.Height,mode.RedBits,mode.GreenBits,mode.BlueBits );
}

int BBGlfwGame::GetDeviceWidth(){
	int width,height;
	glfwGetWindowSize( &width,&height );
	return width;
}

int BBGlfwGame::GetDeviceHeight(){
	int width,height;
	glfwGetWindowSize( &width,&height );
	return height;
}

void BBGlfwGame::SetDeviceWindow( int width,int height,int flags ){

	SetGlfwWindow( width,height,8,8,8,0,CFG_OPENGL_DEPTH_BUFFER_ENABLED ? 32 : 0,0,(flags&1)!=0 );
}

Array<BBDisplayMode*> BBGlfwGame::GetDisplayModes(){

	GLFWvidmode vmodes[1024];
	int n=glfwGetVideoModes( vmodes,1024 );
	Array<BBDisplayMode*> modes( n );
	for( int i=0;i<n;++i ) modes[i]=new BBDisplayMode( vmodes[i].Width,vmodes[i].Height );
	return modes;
}

BBDisplayMode *BBGlfwGame::GetDesktopMode(){

	GLFWvidmode vmode;
	glfwGetDesktopMode( &vmode );
	return new BBDisplayMode( vmode.Width,vmode.Height );
}

void BBGlfwGame::SetSwapInterval( int interval ){
	_swapInterval=interval;
	if( _swapInterval>=0 ) glfwSwapInterval( _swapInterval );
}

void BBGlfwGame::UpdateEvents(){

	if( _suspended ){
		glfwWaitEvents();
	}else{
		glfwPollEvents();
	}
	if( glfwGetWindowParam( GLFW_ACTIVE ) ){
		if( _suspended ){
			ResumeGame();
			_nextUpdate=0;
		}
	}else if( glfwGetWindowParam( GLFW_ICONIFIED ) || CFG_MOJO_AUTO_SUSPEND_ENABLED ){
		if( !_suspended ){
			SuspendGame();
			_nextUpdate=0;
		}
	}
}

void BBGlfwGame::Run(){

#if	CFG_GLFW_WINDOW_WIDTH && CFG_GLFW_WINDOW_HEIGHT

	SetGlfwWindow( CFG_GLFW_WINDOW_WIDTH,CFG_GLFW_WINDOW_HEIGHT,8,8,8,0,CFG_OPENGL_DEPTH_BUFFER_ENABLED ? 32 : 0,0,CFG_GLFW_WINDOW_FULLSCREEN );

#endif

	StartGame();
	
	while( glfwGetWindowParam( GLFW_OPENED ) ){
	
		RenderGame();

		glfwSwapBuffers();
		
		if( _nextUpdate ){
			double delay=_nextUpdate-glfwGetTime();
			if( delay>0 ) glfwSleep( delay );
		}
		
		//Update user events
		UpdateEvents();

		//App suspended?		
		if( _suspended ) continue;

		//'Go nuts' mode!
		if( !_updateRate ){
			UpdateGame();
			continue;
		}
		
		//Reset update timer?
		if( !_nextUpdate ) _nextUpdate=glfwGetTime();
		
		//Catch up updates...
		int i=0;
		for( ;i<4;++i ){
		
			UpdateGame();
			if( !_nextUpdate ) break;
			
			_nextUpdate+=_updatePeriod;
			
			if( _nextUpdate>glfwGetTime() ) break;
		}
		
		if( i==4 ) _nextUpdate=0;
	}
}



//***** monkeygame.h *****

class BBMonkeyGame : public BBGlfwGame{
public:

	static void Main( int args,const char *argv[] );
};

//***** monkeygame.cpp *****

#define _QUOTE(X) #X
#define _STRINGIZE(X) _QUOTE(X)

void BBMonkeyGame::Main( int argc,const char *argv[] ){

	if( !glfwInit() ){
		puts( "glfwInit failed" );
		exit(-1);
	}

	BBMonkeyGame *game=new BBMonkeyGame();
	
	try{
	
		bb_std_main( argc,argv );
		
	}catch( ThrowableObject *ex ){
	
		glfwTerminate();
		
		game->Die( ex );
		
		return;
	}

	if( game->Delegate() ) game->Run();
	
	glfwTerminate();
}


// GLFW mojo runtime.
//
// Copyright 2011 Mark Sibly, all rights reserved.
// No warranty implied; use at your own risk.

//***** gxtkGraphics.h *****

class gxtkSurface;

class gxtkGraphics : public Object{
public:

	enum{
		MAX_VERTS=1024,
		MAX_QUADS=(MAX_VERTS/4)
	};

	int width;
	int height;

	int colorARGB;
	float r,g,b,alpha;
	float ix,iy,jx,jy,tx,ty;
	bool tformed;

	float vertices[MAX_VERTS*5];
	unsigned short quadIndices[MAX_QUADS*6];

	int primType;
	int vertCount;
	gxtkSurface *primSurf;
	
	gxtkGraphics();
	
	void Flush();
	float *Begin( int type,int count,gxtkSurface *surf );
	
	//***** GXTK API *****
	virtual int Width();
	virtual int Height();
	
	virtual int  BeginRender();
	virtual void EndRender();
	virtual void DiscardGraphics();

	virtual gxtkSurface *LoadSurface( String path );
	virtual gxtkSurface *CreateSurface( int width,int height );
	virtual bool LoadSurface__UNSAFE__( gxtkSurface *surface,String path );
	
	virtual int Cls( float r,float g,float b );
	virtual int SetAlpha( float alpha );
	virtual int SetColor( float r,float g,float b );
	virtual int SetBlend( int blend );
	virtual int SetScissor( int x,int y,int w,int h );
	virtual int SetMatrix( float ix,float iy,float jx,float jy,float tx,float ty );
	
	virtual int DrawPoint( float x,float y );
	virtual int DrawRect( float x,float y,float w,float h );
	virtual int DrawLine( float x1,float y1,float x2,float y2 );
	virtual int DrawOval( float x1,float y1,float x2,float y2 );
	virtual int DrawPoly( Array<Float> verts );
	virtual int DrawPoly2( Array<Float> verts,gxtkSurface *surface,int srcx,int srcy );
	virtual int DrawSurface( gxtkSurface *surface,float x,float y );
	virtual int DrawSurface2( gxtkSurface *surface,float x,float y,int srcx,int srcy,int srcw,int srch );
	
	virtual int ReadPixels( Array<int> pixels,int x,int y,int width,int height,int offset,int pitch );
	virtual int WritePixels2( gxtkSurface *surface,Array<int> pixels,int x,int y,int width,int height,int offset,int pitch );
};

class gxtkSurface : public Object{
public:
	unsigned char *data;
	int width;
	int height;
	int depth;
	int format;
	int seq;
	
	GLuint texture;
	float uscale;
	float vscale;
	
	gxtkSurface();
	
	void SetData( unsigned char *data,int width,int height,int depth );
	void SetSubData( int x,int y,int w,int h,unsigned *src,int pitch );
	void Bind();
	
	~gxtkSurface();
	
	//***** GXTK API *****
	virtual int Discard();
	virtual int Width();
	virtual int Height();
	virtual int Loaded();
	virtual void OnUnsafeLoadComplete();
};

//***** gxtkGraphics.cpp *****

#ifndef GL_BGRA
#define GL_BGRA  0x80e1
#endif

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812f
#endif

#ifndef GL_GENERATE_MIPMAP
#define GL_GENERATE_MIPMAP 0x8191
#endif

static int Pow2Size( int n ){
	int i=1;
	while( i<n ) i+=i;
	return i;
}

gxtkGraphics::gxtkGraphics(){

	width=height=0;
	vertCount=0;
	
#ifdef _glfw3_h_
	GLFWwindow *window=BBGlfwGame::GlfwGame()->GetGLFWwindow();
	if( window ) glfwGetWindowSize( BBGlfwGame::GlfwGame()->GetGLFWwindow(),&width,&height );
#else
	glfwGetWindowSize( &width,&height );
#endif
	
	if( CFG_OPENGL_GLES20_ENABLED ) return;
	
	for( int i=0;i<MAX_QUADS;++i ){
		quadIndices[i*6  ]=(short)(i*4);
		quadIndices[i*6+1]=(short)(i*4+1);
		quadIndices[i*6+2]=(short)(i*4+2);
		quadIndices[i*6+3]=(short)(i*4);
		quadIndices[i*6+4]=(short)(i*4+2);
		quadIndices[i*6+5]=(short)(i*4+3);
	}
}

void gxtkGraphics::Flush(){
	if( !vertCount ) return;

	if( primSurf ){
		glEnable( GL_TEXTURE_2D );
		primSurf->Bind();
	}
		
	switch( primType ){
	case 1:
		glDrawArrays( GL_POINTS,0,vertCount );
		break;
	case 2:
		glDrawArrays( GL_LINES,0,vertCount );
		break;
	case 3:
		glDrawArrays( GL_TRIANGLES,0,vertCount );
		break;
	case 4:
		glDrawElements( GL_TRIANGLES,vertCount/4*6,GL_UNSIGNED_SHORT,quadIndices );
		break;
	default:
		for( int j=0;j<vertCount;j+=primType ){
			glDrawArrays( GL_TRIANGLE_FAN,j,primType );
		}
		break;
	}

	if( primSurf ){
		glDisable( GL_TEXTURE_2D );
	}

	vertCount=0;
}

float *gxtkGraphics::Begin( int type,int count,gxtkSurface *surf ){
	if( primType!=type || primSurf!=surf || vertCount+count>MAX_VERTS ){
		Flush();
		primType=type;
		primSurf=surf;
	}
	float *vp=vertices+vertCount*5;
	vertCount+=count;
	return vp;
}

//***** GXTK API *****

int gxtkGraphics::Width(){
	return width;
}

int gxtkGraphics::Height(){
	return height;
}

int gxtkGraphics::BeginRender(){

	width=height=0;
#ifdef _glfw3_h_
	glfwGetWindowSize( BBGlfwGame::GlfwGame()->GetGLFWwindow(),&width,&height );
#else
	glfwGetWindowSize( &width,&height );
#endif

#if CFG_OPENGL_GLES20_ENABLED
	return 0;
#else

	glViewport( 0,0,width,height );

	glMatrixMode( GL_PROJECTION );
	glLoadIdentity();
	glOrtho( 0,width,height,0,-1,1 );
	glMatrixMode( GL_MODELVIEW );
	glLoadIdentity();
	
	glEnableClientState( GL_VERTEX_ARRAY );
	glVertexPointer( 2,GL_FLOAT,20,&vertices[0] );	
	
	glEnableClientState( GL_TEXTURE_COORD_ARRAY );
	glTexCoordPointer( 2,GL_FLOAT,20,&vertices[2] );
	
	glEnableClientState( GL_COLOR_ARRAY );
	glColorPointer( 4,GL_UNSIGNED_BYTE,20,&vertices[4] );
	
	glEnable( GL_BLEND );
	glBlendFunc( GL_ONE,GL_ONE_MINUS_SRC_ALPHA );
	
	glDisable( GL_TEXTURE_2D );
	
	vertCount=0;
	
	return 1;
	
#endif
}

void gxtkGraphics::EndRender(){
	if( !CFG_OPENGL_GLES20_ENABLED ) Flush();
}

void gxtkGraphics::DiscardGraphics(){
}

int gxtkGraphics::Cls( float r,float g,float b ){
	vertCount=0;

	glClearColor( r/255.0f,g/255.0f,b/255.0f,1 );
	glClear( GL_COLOR_BUFFER_BIT );

	return 0;
}

int gxtkGraphics::SetAlpha( float alpha ){
	this->alpha=alpha;
	
	int a=int(alpha*255);
	
	colorARGB=(a<<24) | (int(b*alpha)<<16) | (int(g*alpha)<<8) | int(r*alpha);
	
	return 0;
}

int gxtkGraphics::SetColor( float r,float g,float b ){
	this->r=r;
	this->g=g;
	this->b=b;

	int a=int(alpha*255);
	
	colorARGB=(a<<24) | (int(b*alpha)<<16) | (int(g*alpha)<<8) | int(r*alpha);
	
	return 0;
}

int gxtkGraphics::SetBlend( int blend ){

	Flush();
	
	switch( blend ){
	case 1:
		glBlendFunc( GL_ONE,GL_ONE );
		break;
	default:
		glBlendFunc( GL_ONE,GL_ONE_MINUS_SRC_ALPHA );
	}

	return 0;
}

int gxtkGraphics::SetScissor( int x,int y,int w,int h ){

	Flush();
	
	if( x!=0 || y!=0 || w!=Width() || h!=Height() ){
		glEnable( GL_SCISSOR_TEST );
		y=Height()-y-h;
		glScissor( x,y,w,h );
	}else{
		glDisable( GL_SCISSOR_TEST );
	}
	return 0;
}

int gxtkGraphics::SetMatrix( float ix,float iy,float jx,float jy,float tx,float ty ){

	tformed=(ix!=1 || iy!=0 || jx!=0 || jy!=1 || tx!=0 || ty!=0);

	this->ix=ix;this->iy=iy;this->jx=jx;this->jy=jy;this->tx=tx;this->ty=ty;

	return 0;
}

int gxtkGraphics::DrawPoint( float x,float y ){

	if( tformed ){
		float px=x;
		x=px * ix + y * jx + tx;
		y=px * iy + y * jy + ty;
	}
	
	float *vp=Begin( 1,1,0 );
	
	vp[0]=x+.5f;vp[1]=y+.5f;(int&)vp[4]=colorARGB;

	return 0;	
}
	
int gxtkGraphics::DrawLine( float x0,float y0,float x1,float y1 ){

	if( tformed ){
		float tx0=x0,tx1=x1;
		x0=tx0 * ix + y0 * jx + tx;y0=tx0 * iy + y0 * jy + ty;
		x1=tx1 * ix + y1 * jx + tx;y1=tx1 * iy + y1 * jy + ty;
	}
	
	float *vp=Begin( 2,2,0 );

	vp[0]=x0+.5f;vp[1]=y0+.5f;(int&)vp[4]=colorARGB;
	vp[5]=x1+.5f;vp[6]=y1+.5f;(int&)vp[9]=colorARGB;
	
	return 0;
}

int gxtkGraphics::DrawRect( float x,float y,float w,float h ){

	float x0=x,x1=x+w,x2=x+w,x3=x;
	float y0=y,y1=y,y2=y+h,y3=y+h;

	if( tformed ){
		float tx0=x0,tx1=x1,tx2=x2,tx3=x3;
		x0=tx0 * ix + y0 * jx + tx;y0=tx0 * iy + y0 * jy + ty;
		x1=tx1 * ix + y1 * jx + tx;y1=tx1 * iy + y1 * jy + ty;
		x2=tx2 * ix + y2 * jx + tx;y2=tx2 * iy + y2 * jy + ty;
		x3=tx3 * ix + y3 * jx + tx;y3=tx3 * iy + y3 * jy + ty;
	}
	
	float *vp=Begin( 4,4,0 );

	vp[0 ]=x0;vp[1 ]=y0;(int&)vp[4 ]=colorARGB;
	vp[5 ]=x1;vp[6 ]=y1;(int&)vp[9 ]=colorARGB;
	vp[10]=x2;vp[11]=y2;(int&)vp[14]=colorARGB;
	vp[15]=x3;vp[16]=y3;(int&)vp[19]=colorARGB;

	return 0;
}

int gxtkGraphics::DrawOval( float x,float y,float w,float h ){
	
	float xr=w/2.0f;
	float yr=h/2.0f;

	int n;
	if( tformed ){
		float dx_x=xr * ix;
		float dx_y=xr * iy;
		float dx=sqrtf( dx_x*dx_x+dx_y*dx_y );
		float dy_x=yr * jx;
		float dy_y=yr * jy;
		float dy=sqrtf( dy_x*dy_x+dy_y*dy_y );
		n=(int)( dx+dy );
	}else{
		n=(int)( fabs( xr )+fabs( yr ) );
	}
	
	if( n<12 ){
		n=12;
	}else if( n>MAX_VERTS ){
		n=MAX_VERTS;
	}else{
		n&=~3;
	}

	float x0=x+xr,y0=y+yr;
	
	float *vp=Begin( n,n,0 );

	for( int i=0;i<n;++i ){
	
		float th=i * 6.28318531f / n;

		float px=x0+cosf( th ) * xr;
		float py=y0-sinf( th ) * yr;
		
		if( tformed ){
			float ppx=px;
			px=ppx * ix + py * jx + tx;
			py=ppx * iy + py * jy + ty;
		}
		
		vp[0]=px;vp[1]=py;(int&)vp[4]=colorARGB;
		vp+=5;
	}
	
	return 0;
}

int gxtkGraphics::DrawPoly( Array<Float> verts ){

	int n=verts.Length()/2;
	if( n<1 || n>MAX_VERTS ) return 0;
	
	float *vp=Begin( n,n,0 );
	
	for( int i=0;i<n;++i ){
		int j=i*2;
		if( tformed ){
			vp[0]=verts[j] * ix + verts[j+1] * jx + tx;
			vp[1]=verts[j] * iy + verts[j+1] * jy + ty;
		}else{
			vp[0]=verts[j];
			vp[1]=verts[j+1];
		}
		(int&)vp[4]=colorARGB;
		vp+=5;
	}

	return 0;
}

int gxtkGraphics::DrawPoly2( Array<Float> verts,gxtkSurface *surface,int srcx,int srcy ){

	int n=verts.Length()/4;
	if( n<1 || n>MAX_VERTS ) return 0;
		
	float *vp=Begin( n,n,surface );
	
	for( int i=0;i<n;++i ){
		int j=i*4;
		if( tformed ){
			vp[0]=verts[j] * ix + verts[j+1] * jx + tx;
			vp[1]=verts[j] * iy + verts[j+1] * jy + ty;
		}else{
			vp[0]=verts[j];
			vp[1]=verts[j+1];
		}
		vp[2]=(srcx+verts[j+2])*surface->uscale;
		vp[3]=(srcy+verts[j+3])*surface->vscale;
		(int&)vp[4]=colorARGB;
		vp+=5;
	}
	
	return 0;
}

int gxtkGraphics::DrawSurface( gxtkSurface *surf,float x,float y ){
	
	float w=surf->Width();
	float h=surf->Height();
	float x0=x,x1=x+w,x2=x+w,x3=x;
	float y0=y,y1=y,y2=y+h,y3=y+h;
	float u0=0,u1=w*surf->uscale;
	float v0=0,v1=h*surf->vscale;

	if( tformed ){
		float tx0=x0,tx1=x1,tx2=x2,tx3=x3;
		x0=tx0 * ix + y0 * jx + tx;y0=tx0 * iy + y0 * jy + ty;
		x1=tx1 * ix + y1 * jx + tx;y1=tx1 * iy + y1 * jy + ty;
		x2=tx2 * ix + y2 * jx + tx;y2=tx2 * iy + y2 * jy + ty;
		x3=tx3 * ix + y3 * jx + tx;y3=tx3 * iy + y3 * jy + ty;
	}
	
	float *vp=Begin( 4,4,surf );
	
	vp[0 ]=x0;vp[1 ]=y0;vp[2 ]=u0;vp[3 ]=v0;(int&)vp[4 ]=colorARGB;
	vp[5 ]=x1;vp[6 ]=y1;vp[7 ]=u1;vp[8 ]=v0;(int&)vp[9 ]=colorARGB;
	vp[10]=x2;vp[11]=y2;vp[12]=u1;vp[13]=v1;(int&)vp[14]=colorARGB;
	vp[15]=x3;vp[16]=y3;vp[17]=u0;vp[18]=v1;(int&)vp[19]=colorARGB;
	
	return 0;
}

int gxtkGraphics::DrawSurface2( gxtkSurface *surf,float x,float y,int srcx,int srcy,int srcw,int srch ){
	
	float w=srcw;
	float h=srch;
	float x0=x,x1=x+w,x2=x+w,x3=x;
	float y0=y,y1=y,y2=y+h,y3=y+h;
	float u0=srcx*surf->uscale,u1=(srcx+srcw)*surf->uscale;
	float v0=srcy*surf->vscale,v1=(srcy+srch)*surf->vscale;

	if( tformed ){
		float tx0=x0,tx1=x1,tx2=x2,tx3=x3;
		x0=tx0 * ix + y0 * jx + tx;y0=tx0 * iy + y0 * jy + ty;
		x1=tx1 * ix + y1 * jx + tx;y1=tx1 * iy + y1 * jy + ty;
		x2=tx2 * ix + y2 * jx + tx;y2=tx2 * iy + y2 * jy + ty;
		x3=tx3 * ix + y3 * jx + tx;y3=tx3 * iy + y3 * jy + ty;
	}
	
	float *vp=Begin( 4,4,surf );
	
	vp[0 ]=x0;vp[1 ]=y0;vp[2 ]=u0;vp[3 ]=v0;(int&)vp[4 ]=colorARGB;
	vp[5 ]=x1;vp[6 ]=y1;vp[7 ]=u1;vp[8 ]=v0;(int&)vp[9 ]=colorARGB;
	vp[10]=x2;vp[11]=y2;vp[12]=u1;vp[13]=v1;(int&)vp[14]=colorARGB;
	vp[15]=x3;vp[16]=y3;vp[17]=u0;vp[18]=v1;(int&)vp[19]=colorARGB;
	
	return 0;
}
	
int gxtkGraphics::ReadPixels( Array<int> pixels,int x,int y,int width,int height,int offset,int pitch ){

	Flush();

	unsigned *p=(unsigned*)malloc(width*height*4);

	glReadPixels( x,this->height-y-height,width,height,GL_BGRA,GL_UNSIGNED_BYTE,p );
	
	for( int py=0;py<height;++py ){
		memcpy( &pixels[offset+py*pitch],&p[(height-py-1)*width],width*4 );
	}
	
	free( p );
	
	return 0;
}

int gxtkGraphics::WritePixels2( gxtkSurface *surface,Array<int> pixels,int x,int y,int width,int height,int offset,int pitch ){

	surface->SetSubData( x,y,width,height,(unsigned*)&pixels[offset],pitch );
	
	return 0;
}

//***** gxtkSurface *****

gxtkSurface::gxtkSurface():data(0),width(0),height(0),depth(0),format(0),seq(-1),texture(0),uscale(0),vscale(0){
}

gxtkSurface::~gxtkSurface(){
	Discard();
}

int gxtkSurface::Discard(){
	if( seq==glfwGraphicsSeq ){
		glDeleteTextures( 1,&texture );
		seq=-1;
	}
	if( data ){
		free( data );
		data=0;
	}
	return 0;
}

int gxtkSurface::Width(){
	return width;
}

int gxtkSurface::Height(){
	return height;
}

int gxtkSurface::Loaded(){
	return 1;
}

//Careful! Can't call any GL here as it may be executing off-thread.
//
void gxtkSurface::SetData( unsigned char *data,int width,int height,int depth ){

	this->data=data;
	this->width=width;
	this->height=height;
	this->depth=depth;
	
	unsigned char *p=data;
	int n=width*height;
	
	switch( depth ){
	case 1:
		format=GL_LUMINANCE;
		break;
	case 2:
		format=GL_LUMINANCE_ALPHA;
		if( data ){
			while( n-- ){	//premultiply alpha
				p[0]=p[0]*p[1]/255;
				p+=2;
			}
		}
		break;
	case 3:
		format=GL_RGB;
		break;
	case 4:
		format=GL_RGBA;
		if( data ){
			while( n-- ){	//premultiply alpha
				p[0]=p[0]*p[3]/255;
				p[1]=p[1]*p[3]/255;
				p[2]=p[2]*p[3]/255;
				p+=4;
			}
		}
		break;
	}
}

void gxtkSurface::SetSubData( int x,int y,int w,int h,unsigned *src,int pitch ){
	if( format!=GL_RGBA ) return;
	
	if( !data ) data=(unsigned char*)malloc( width*height*4 );
	
	unsigned *dst=(unsigned*)data+y*width+x;
	
	for( int py=0;py<h;++py ){
		unsigned *d=dst+py*width;
		unsigned *s=src+py*pitch;
		for( int px=0;px<w;++px ){
			unsigned p=*s++;
			unsigned a=p>>24;
			*d++=(a<<24) | ((p>>0&0xff)*a/255<<16) | ((p>>8&0xff)*a/255<<8) | ((p>>16&0xff)*a/255);
		}
	}
	
	if( seq==glfwGraphicsSeq ){
		glBindTexture( GL_TEXTURE_2D,texture );
		glPixelStorei( GL_UNPACK_ALIGNMENT,1 );
		if( width==pitch ){
			glTexSubImage2D( GL_TEXTURE_2D,0,x,y,w,h,format,GL_UNSIGNED_BYTE,dst );
		}else{
			for( int py=0;py<h;++py ){
				glTexSubImage2D( GL_TEXTURE_2D,0,x,y+py,w,1,format,GL_UNSIGNED_BYTE,dst+py*width );
			}
		}
	}
}

void gxtkSurface::Bind(){

	if( !glfwGraphicsSeq ) return;
	
	if( seq==glfwGraphicsSeq ){
		glBindTexture( GL_TEXTURE_2D,texture );
		return;
	}
	
	seq=glfwGraphicsSeq;
	
	glGenTextures( 1,&texture );
	glBindTexture( GL_TEXTURE_2D,texture );
	
	if( CFG_MOJO_IMAGE_FILTERING_ENABLED ){
		glTexParameteri( GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR );
	}else{
		glTexParameteri( GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST );
		glTexParameteri( GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST );
	}

	glTexParameteri( GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE );

	int texwidth=width;
	int texheight=height;
	
	glTexImage2D( GL_TEXTURE_2D,0,format,texwidth,texheight,0,format,GL_UNSIGNED_BYTE,0 );
	if( glGetError()!=GL_NO_ERROR ){
		texwidth=Pow2Size( width );
		texheight=Pow2Size( height );
		glTexImage2D( GL_TEXTURE_2D,0,format,texwidth,texheight,0,format,GL_UNSIGNED_BYTE,0 );
	}
	
	uscale=1.0/texwidth;
	vscale=1.0/texheight;
	
	if( data ){
		glPixelStorei( GL_UNPACK_ALIGNMENT,1 );
		glTexSubImage2D( GL_TEXTURE_2D,0,0,0,width,height,format,GL_UNSIGNED_BYTE,data );
	}
}

void gxtkSurface::OnUnsafeLoadComplete(){
	Bind();
}

bool gxtkGraphics::LoadSurface__UNSAFE__( gxtkSurface *surface,String path ){

	int width,height,depth;
	unsigned char *data=BBGlfwGame::GlfwGame()->LoadImageData( path,&width,&height,&depth );
	if( !data ) return false;
	
	surface->SetData( data,width,height,depth );
	return true;
}

gxtkSurface *gxtkGraphics::LoadSurface( String path ){
	gxtkSurface *surf=new gxtkSurface();
	if( !LoadSurface__UNSAFE__( surf,path ) ) return 0;
	surf->Bind();
	return surf;
}

gxtkSurface *gxtkGraphics::CreateSurface( int width,int height ){
	gxtkSurface *surf=new gxtkSurface();
	surf->SetData( 0,width,height,4 );
	surf->Bind();
	return surf;
}

//***** gxtkAudio.h *****

class gxtkSample;

class gxtkChannel{
public:
	ALuint source;
	gxtkSample *sample;
	int flags;
	int state;
	
	int AL_Source();
};

class gxtkAudio : public Object{
public:
	static gxtkAudio *audio;
	
	ALCdevice *alcDevice;
	ALCcontext *alcContext;
	gxtkChannel channels[33];

	gxtkAudio();

	virtual void mark();

	//***** GXTK API *****
	virtual int Suspend();
	virtual int Resume();

	virtual gxtkSample *LoadSample( String path );
	virtual bool LoadSample__UNSAFE__( gxtkSample *sample,String path );
	
	virtual int PlaySample( gxtkSample *sample,int channel,int flags );

	virtual int StopChannel( int channel );
	virtual int PauseChannel( int channel );
	virtual int ResumeChannel( int channel );
	virtual int ChannelState( int channel );
	virtual int SetVolume( int channel,float volume );
	virtual int SetPan( int channel,float pan );
	virtual int SetRate( int channel,float rate );
	
	virtual int PlayMusic( String path,int flags );
	virtual int StopMusic();
	virtual int PauseMusic();
	virtual int ResumeMusic();
	virtual int MusicState();
	virtual int SetMusicVolume( float volume );
};

class gxtkSample : public Object{
public:
	ALuint al_buffer;

	gxtkSample();
	gxtkSample( ALuint buf );
	~gxtkSample();
	
	void SetBuffer( ALuint buf );
	
	//***** GXTK API *****
	virtual int Discard();
};

//***** gxtkAudio.cpp *****

gxtkAudio *gxtkAudio::audio;

static std::vector<ALuint> discarded;

static void FlushDiscarded(){

	if( !discarded.size() ) return;
	
	for( int i=0;i<33;++i ){
		gxtkChannel *chan=&gxtkAudio::audio->channels[i];
		if( chan->state ){
			int state=0;
			alGetSourcei( chan->source,AL_SOURCE_STATE,&state );
			if( state==AL_STOPPED ) alSourcei( chan->source,AL_BUFFER,0 );
		}
	}
	
	std::vector<ALuint> out;
	
	for( int i=0;i<discarded.size();++i ){
		ALuint buf=discarded[i];
		alDeleteBuffers( 1,&buf );
		ALenum err=alGetError();
		if( err==AL_NO_ERROR ){
//			printf( "alDeleteBuffers OK!\n" );fflush( stdout );
		}else{
//			printf( "alDeleteBuffers failed...\n" );fflush( stdout );
			out.push_back( buf );
		}
	}
	discarded=out;
}

int gxtkChannel::AL_Source(){
	if( source ) return source;

	alGetError();
	alGenSources( 1,&source );
	if( alGetError()==AL_NO_ERROR ) return source;
	
	//couldn't create source...steal a free source...?
	//
	source=0;
	for( int i=0;i<32;++i ){
		gxtkChannel *chan=&gxtkAudio::audio->channels[i];
		if( !chan->source || gxtkAudio::audio->ChannelState( i ) ) continue;
//		puts( "Stealing source!" );
		source=chan->source;
		chan->source=0;
		break;
	}
	return source;
}

gxtkAudio::gxtkAudio(){

	audio=this;
	
	alcDevice=alcOpenDevice( 0 );
	if( !alcDevice ){
		alcDevice=alcOpenDevice( "Generic Hardware" );
		if( !alcDevice ) alcDevice=alcOpenDevice( "Generic Software" );
	}

//	bbPrint( "opening openal device" );
	if( alcDevice ){
		if( (alcContext=alcCreateContext( alcDevice,0 )) ){
			if( (alcMakeContextCurrent( alcContext )) ){
				//alc all go!
			}else{
				bbPrint( "OpenAl error: alcMakeContextCurrent failed" );
			}
		}else{
			bbPrint( "OpenAl error: alcCreateContext failed" );
		}
	}else{
		bbPrint( "OpenAl error: alcOpenDevice failed" );
	}

	alDistanceModel( AL_NONE );
	
	memset( channels,0,sizeof(channels) );

	channels[32].AL_Source();
}

void gxtkAudio::mark(){
	for( int i=0;i<33;++i ){
		gxtkChannel *chan=&channels[i];
		if( chan->state!=0 ){
			int state=0;
			alGetSourcei( chan->source,AL_SOURCE_STATE,&state );
			if( state!=AL_STOPPED ) gc_mark( chan->sample );
		}
	}
}

int gxtkAudio::Suspend(){
	for( int i=0;i<33;++i ){
		gxtkChannel *chan=&channels[i];
		if( chan->state==1 ){
			int state=0;
			alGetSourcei( chan->source,AL_SOURCE_STATE,&state );
			if( state==AL_PLAYING ) alSourcePause( chan->source );
		}
	}
	return 0;
}

int gxtkAudio::Resume(){
	for( int i=0;i<33;++i ){
		gxtkChannel *chan=&channels[i];
		if( chan->state==1 ){
			int state=0;
			alGetSourcei( chan->source,AL_SOURCE_STATE,&state );
			if( state==AL_PAUSED ) alSourcePlay( chan->source );
		}
	}
	return 0;
}

bool gxtkAudio::LoadSample__UNSAFE__( gxtkSample *sample,String path ){

	int length=0;
	int channels=0;
	int format=0;
	int hertz=0;
	unsigned char *data=BBGlfwGame::GlfwGame()->LoadAudioData( path,&length,&channels,&format,&hertz );
	if( !data ) return false;
	
	int al_format=0;
	if( format==1 && channels==1 ){
		al_format=AL_FORMAT_MONO8;
	}else if( format==1 && channels==2 ){
		al_format=AL_FORMAT_STEREO8;
	}else if( format==2 && channels==1 ){
		al_format=AL_FORMAT_MONO16;
	}else if( format==2 && channels==2 ){
		al_format=AL_FORMAT_STEREO16;
	}
	
	int size=length*channels*format;
	
	ALuint al_buffer;
	alGenBuffers( 1,&al_buffer );
	alBufferData( al_buffer,al_format,data,size,hertz );
	free( data );
	
	sample->SetBuffer( al_buffer );
	return true;
}

gxtkSample *gxtkAudio::LoadSample( String path ){
	FlushDiscarded();
	gxtkSample *sample=new gxtkSample();
	if( !LoadSample__UNSAFE__( sample,path ) ) return 0;
	return sample;
}

int gxtkAudio::PlaySample( gxtkSample *sample,int channel,int flags ){

	FlushDiscarded();
	
	gxtkChannel *chan=&channels[channel];
	
	if( !chan->AL_Source() ) return -1;
	
	alSourceStop( chan->source );
	alSourcei( chan->source,AL_BUFFER,sample->al_buffer );
	alSourcei( chan->source,AL_LOOPING,flags ? 1 : 0 );
	alSourcePlay( chan->source );
	
	gc_assign( chan->sample,sample );

	chan->flags=flags;
	chan->state=1;

	return 0;
}

int gxtkAudio::StopChannel( int channel ){
	gxtkChannel *chan=&channels[channel];

	if( chan->state!=0 ){
		alSourceStop( chan->source );
		chan->state=0;
	}
	return 0;
}

int gxtkAudio::PauseChannel( int channel ){
	gxtkChannel *chan=&channels[channel];

	if( chan->state==1 ){
		int state=0;
		alGetSourcei( chan->source,AL_SOURCE_STATE,&state );
		if( state==AL_STOPPED ){
			chan->state=0;
		}else{
			alSourcePause( chan->source );
			chan->state=2;
		}
	}
	return 0;
}

int gxtkAudio::ResumeChannel( int channel ){
	gxtkChannel *chan=&channels[channel];

	if( chan->state==2 ){
		alSourcePlay( chan->source );
		chan->state=1;
	}
	return 0;
}

int gxtkAudio::ChannelState( int channel ){
	gxtkChannel *chan=&channels[channel];
	
	if( chan->state==1 ){
		int state=0;
		alGetSourcei( chan->source,AL_SOURCE_STATE,&state );
		if( state==AL_STOPPED ) chan->state=0;
	}
	return chan->state;
}

int gxtkAudio::SetVolume( int channel,float volume ){
	gxtkChannel *chan=&channels[channel];

	alSourcef( chan->AL_Source(),AL_GAIN,volume );
	return 0;
}

int gxtkAudio::SetPan( int channel,float pan ){
	gxtkChannel *chan=&channels[channel];
	
	float x=sinf( pan ),y=0,z=-cosf( pan );
	alSource3f( chan->AL_Source(),AL_POSITION,x,y,z );
	return 0;
}

int gxtkAudio::SetRate( int channel,float rate ){
	gxtkChannel *chan=&channels[channel];

	alSourcef( chan->AL_Source(),AL_PITCH,rate );
	return 0;
}

int gxtkAudio::PlayMusic( String path,int flags ){
	StopMusic();
	
	gxtkSample *music=LoadSample( path );
	if( !music ) return -1;
	
	PlaySample( music,32,flags );
	return 0;
}

int gxtkAudio::StopMusic(){
	StopChannel( 32 );
	return 0;
}

int gxtkAudio::PauseMusic(){
	PauseChannel( 32 );
	return 0;
}

int gxtkAudio::ResumeMusic(){
	ResumeChannel( 32 );
	return 0;
}

int gxtkAudio::MusicState(){
	return ChannelState( 32 );
}

int gxtkAudio::SetMusicVolume( float volume ){
	SetVolume( 32,volume );
	return 0;
}

gxtkSample::gxtkSample():
al_buffer(0){
}

gxtkSample::gxtkSample( ALuint buf ):
al_buffer(buf){
}

gxtkSample::~gxtkSample(){
	Discard();
}

void gxtkSample::SetBuffer( ALuint buf ){
	al_buffer=buf;
}

int gxtkSample::Discard(){
	if( al_buffer ){
		discarded.push_back( al_buffer );
		al_buffer=0;
	}
	return 0;
}


// ***** thread.h *****

#if __cplusplus_winrt

using namespace Windows::System::Threading;

#endif

class BBThread : public Object{
public:
	Object *result;
	
	BBThread();
	
	virtual void Start();
	virtual bool IsRunning();
	
	virtual Object *Result();
	virtual void SetResult( Object *result );
	
	static  String Strdup( const String &str );
	
	virtual void Run__UNSAFE__();
	
	
private:

	enum{
		INIT=0,
		RUNNING=1,
		FINISHED=2
	};

	
	int _state;
	Object *_result;
	
#if __cplusplus_winrt

	friend class Launcher;

	class Launcher{
	
		friend class BBThread;
		BBThread *_thread;
		
		Launcher( BBThread *thread ):_thread(thread){
		}
		
		public:
		
		void operator()( IAsyncAction ^operation ){
			_thread->Run__UNSAFE__();
			_thread->_state=FINISHED;
		} 
	};
	
#elif _WIN32

	static DWORD WINAPI run( void *p );
	
#else

	static void *run( void *p );
	
#endif

};

// ***** thread.cpp *****

BBThread::BBThread():_state( INIT ),_result( 0 ){
}

bool BBThread::IsRunning(){
	return _state==RUNNING;
}

Object *BBThread::Result(){
	return _result;
}

void BBThread::SetResult( Object *result ){
	_result=result;
}

String BBThread::Strdup( const String &str ){
	return str.Copy();
}

void BBThread::Run__UNSAFE__(){
}

#if __cplusplus_winrt

void BBThread::Start(){
	if( _state==RUNNING ) return;
	
	_result=0;
	_state=RUNNING;
	
	Launcher launcher( this );
	
	auto handler=ref new WorkItemHandler( launcher );
	
	ThreadPool::RunAsync( handler );
}

#elif _WIN32

void BBThread::Start(){
	if( _state==RUNNING ) return;
	
	_result=0;
	_state=RUNNING;
	
	DWORD _id;
	HANDLE _handle;

	if( _handle=CreateThread( 0,0,run,this,0,&_id ) ){
		CloseHandle( _handle );
		return;
	}
	
	puts( "CreateThread failed!" );
	exit( -1 );
}

DWORD WINAPI BBThread::run( void *p ){
	BBThread *thread=(BBThread*)p;

	thread->Run__UNSAFE__();
	
	thread->_state=FINISHED;
	return 0;
}

#else

void BBThread::Start(){
	if( _state==RUNNING ) return;
	
	_result=0;
	_state=RUNNING;
	
	pthread_t _handle;
	
	if( !pthread_create( &_handle,0,run,this ) ){
		pthread_detach( _handle );
		return;
	}
	
	puts( "pthread_create failed!" );
	exit( -1 );
}

void *BBThread::run( void *p ){
	BBThread *thread=(BBThread*)p;

	thread->Run__UNSAFE__();

	thread->_state=FINISHED;
	return 0;
}

#endif


// ***** databuffer.h *****

class BBDataBuffer : public Object{
public:
	
	BBDataBuffer();
	
	~BBDataBuffer();
	
	bool _New( int length,void *data=0 );
	
	bool _Load( String path );
	
	void _LoadAsync( const String &path,BBThread *thread );

	void Discard();
	
	const void *ReadPointer( int offset=0 ){
		return _data+offset;
	}
	
	void *WritePointer( int offset=0 ){
		return _data+offset;
	}
	
	int Length(){
		return _length;
	}
	
	void PokeByte( int addr,int value ){
		*(_data+addr)=value;
	}

	void PokeShort( int addr,int value ){
		*(short*)(_data+addr)=value;
	}
	
	void PokeInt( int addr,int value ){
		*(int*)(_data+addr)=value;
	}
	
	void PokeFloat( int addr,float value ){
		*(float*)(_data+addr)=value;
	}

	int PeekByte( int addr ){
		return *(_data+addr);
	}
	
	int PeekShort( int addr ){
		return *(short*)(_data+addr);
	}
	
	int PeekInt( int addr ){
		return *(int*)(_data+addr);
	}
	
	float PeekFloat( int addr ){
		return *(float*)(_data+addr);
	}
	
private:
	signed char *_data;
	int _length;
};

// ***** databuffer.cpp *****

BBDataBuffer::BBDataBuffer():_data(0),_length(0){
}

BBDataBuffer::~BBDataBuffer(){
	if( _data ) free( _data );
}

bool BBDataBuffer::_New( int length,void *data ){
	if( _data ) return false;
	if( !data ) data=malloc( length );
	_data=(signed char*)data;
	_length=length;
	return true;
}

bool BBDataBuffer::_Load( String path ){
	if( _data ) return false;
	
	_data=(signed char*)BBGame::Game()->LoadData( path,&_length );
	if( !_data ) return false;
	
	return true;
}

void BBDataBuffer::_LoadAsync( const String &cpath,BBThread *thread ){

	String path=cpath.Copy();
	
	if( _Load( path ) ) thread->SetResult( this );
}

void BBDataBuffer::Discard(){
	if( !_data ) return;
	free( _data );
	_data=0;
	_length=0;
}


// ***** stream.h *****

class BBStream : public Object{
public:

	virtual int Eof(){
		return 0;
	}

	virtual void Close(){
	}

	virtual int Length(){
		return 0;
	}
	
	virtual int Position(){
		return 0;
	}
	
	virtual int Seek( int position ){
		return 0;
	}
	
	virtual int Read( BBDataBuffer *buffer,int offset,int count ){
		return 0;
	}

	virtual int Write( BBDataBuffer *buffer,int offset,int count ){
		return 0;
	}
};

// ***** stream.cpp *****


// ***** filestream.h *****

class BBFileStream : public BBStream{
public:

	BBFileStream();
	~BBFileStream();

	void Close();
	int Eof();
	int Length();
	int Position();
	int Seek( int position );
	int Read( BBDataBuffer *buffer,int offset,int count );
	int Write( BBDataBuffer *buffer,int offset,int count );

	bool Open( String path,String mode );
	
private:
	FILE *_file;
	int _position;
	int _length;
};

// ***** filestream.cpp *****

BBFileStream::BBFileStream():_file(0),_position(0),_length(0){
}

BBFileStream::~BBFileStream(){
	if( _file ) fclose( _file );
}

bool BBFileStream::Open( String path,String mode ){
	if( _file ) return false;

	String fmode;	
	if( mode=="r" ){
		fmode="rb";
	}else if( mode=="w" ){
		fmode="wb";
	}else if( mode=="u" ){
		fmode="rb+";
	}else{
		return false;
	}

	_file=BBGame::Game()->OpenFile( path,fmode );
	if( !_file && mode=="u" ) _file=BBGame::Game()->OpenFile( path,"wb+" );
	if( !_file ) return false;
	
	fseek( _file,0,SEEK_END );
	_length=ftell( _file );
	fseek( _file,0,SEEK_SET );
	_position=0;
	
	return true;
}

void BBFileStream::Close(){
	if( !_file ) return;
	
	fclose( _file );
	_file=0;
	_position=0;
	_length=0;
}

int BBFileStream::Eof(){
	if( !_file ) return -1;
	
	return _position==_length;
}

int BBFileStream::Length(){
	return _length;
}

int BBFileStream::Position(){
	return _position;
}

int BBFileStream::Seek( int position ){
	if( !_file ) return 0;
	
	fseek( _file,position,SEEK_SET );
	_position=ftell( _file );
	return _position;
}

int BBFileStream::Read( BBDataBuffer *buffer,int offset,int count ){
	if( !_file ) return 0;
	
	int n=fread( buffer->WritePointer(offset),1,count,_file );
	_position+=n;
	return n;
}

int BBFileStream::Write( BBDataBuffer *buffer,int offset,int count ){
	if( !_file ) return 0;
	
	int n=fwrite( buffer->ReadPointer(offset),1,count,_file );
	_position+=n;
	if( _position>_length ) _length=_position;
	return n;
}


#if !WINDOWS_8

// ***** socket.h *****

#if WINDOWS_PHONE_8

#include <Winsock2.h>

typedef int socklen_t;

#elif _WIN32

#include <winsock.h>

typedef int socklen_t;

#else

#include <netdb.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>

#define closesocket close
#define ioctlsocket ioctl

#endif

class BBSocketAddress : public Object{
public:
	sockaddr_in _sa;
	
	BBSocketAddress();
	
	void Set( String host,int port );
	void Set( BBSocketAddress *address );
	void Set( const sockaddr_in &sa );
	
	String Host(){ Validate();return _host; }
	int Port(){ Validate();return _port; }
	
private:
	bool _valid;
	String _host;
	int _port;
	
	void Validate();
};

class BBSocket : public Object{
public:
	enum{
		PROTOCOL_CLIENT=1,
		PROTOCOL_SERVER=2,
		PROTOCOL_DATAGRAM=3
	};
	
	BBSocket();
	BBSocket( int sock );
	~BBSocket();
	
	bool Open( int protocol );
	void Close();
	
	bool Bind( String host,int port );
	bool Connect( String host,int port );
	bool Listen( int backlog );
	bool Accept();
	BBSocket *Accepted();

	int Send( BBDataBuffer *data,int offset,int count );
	int Receive( BBDataBuffer *data,int offset,int count );

	int SendTo( BBDataBuffer *data,int offset,int count,BBSocketAddress *address );
	int ReceiveFrom( BBDataBuffer *data,int offset,int count,BBSocketAddress *address );
	
	void GetLocalAddress( BBSocketAddress *address );
	void GetRemoteAddress( BBSocketAddress *address );
	
	static void InitSockets();
	
protected:
	int _sock;
	int _proto;
	int _accepted;
};

// ***** socket.cpp *****

static void setsockaddr( sockaddr_in *sa,String host,int port ){
	memset( sa,0,sizeof(*sa) );
	sa->sin_family=AF_INET;
	sa->sin_port=htons( port );
	sa->sin_addr.s_addr=htonl( INADDR_ANY );
	
	if( host.Length() && host.Length()<1024 ){
		char buf[1024];
		for( int i=0;i<host.Length();++i ) buf[i]=host[i];
		buf[host.Length()]=0;
		if( hostent *host=gethostbyname( buf ) ){
			if( char *hostip=inet_ntoa(*(in_addr *)*host->h_addr_list) ){
				sa->sin_addr.s_addr=inet_addr( hostip );
			}
		}
	}
}

void BBSocket::InitSockets(){
#if _WIN32
	static bool started;
	if( !started ){
		WSADATA ws;
		WSAStartup( 0x101,&ws );
		started=true;
	}
#endif
}

BBSocketAddress::BBSocketAddress():_valid( false ){
	BBSocket::InitSockets();
	memset( &_sa,0,sizeof(_sa) );
	_sa.sin_family=AF_INET;
}

void BBSocketAddress::Set( String host,int port ){
	setsockaddr( &_sa,host,port );
	_valid=false;
}

void BBSocketAddress::Set( BBSocketAddress *address ){
	_sa=address->_sa;
	_valid=false;
}

void BBSocketAddress::Set( const sockaddr_in &sa ){
	_sa=sa;
	_valid=false;
}

void BBSocketAddress::Validate(){
	if( _valid ) return;
	_host=String( int(_sa.sin_addr.s_addr)&0xff )+"."+String( int(_sa.sin_addr.s_addr>>8)&0xff )+"."+String( int(_sa.sin_addr.s_addr>>16)&0xff )+"."+String( int(_sa.sin_addr.s_addr>>24)&0xff );
	_port=htons( _sa.sin_port );
	_valid=true;
}

BBSocket::BBSocket():_sock( -1 ){
	BBSocket::InitSockets();
}

BBSocket::BBSocket( int sock ):_sock( sock ){
}

BBSocket::~BBSocket(){

	if( _sock>=0 ) closesocket( _sock );
}

bool BBSocket::Open( int proto ){

	if( _sock>=0 ) return false;
	
	switch( proto ){
	case PROTOCOL_CLIENT:
	case PROTOCOL_SERVER:
		_sock=socket( AF_INET,SOCK_STREAM,IPPROTO_TCP );
		if( _sock>=0 ){
			//nodelay
			int nodelay=1;
			setsockopt( _sock,IPPROTO_TCP,TCP_NODELAY,(const char*)&nodelay,sizeof(nodelay) );
	
			//Do this on Mac so server ports can be quickly reused...
			#if __APPLE__ || __linux
			int flag=1;
			setsockopt( _sock,SOL_SOCKET,SO_REUSEADDR,&flag,sizeof(flag) );
			#endif
		}
		break;
	case PROTOCOL_DATAGRAM:
		_sock=socket( AF_INET,SOCK_DGRAM,IPPROTO_UDP );
		break;
	}
	
	if( _sock<0 ) return false;
	
	_proto=proto;
	return true;
}

void BBSocket::Close(){
	if( _sock<0 ) return;
	closesocket( _sock );
	_sock=-1;
}

void BBSocket::GetLocalAddress( BBSocketAddress *address ){
	sockaddr_in sa;
	memset( &sa,0,sizeof(sa) );
	sa.sin_family=AF_INET;
	socklen_t size=sizeof(sa);
	if( _sock>=0 ) getsockname( _sock,(sockaddr*)&sa,&size );
	address->Set( sa );
}

void BBSocket::GetRemoteAddress( BBSocketAddress *address ){
	sockaddr_in sa;
	memset( &sa,0,sizeof(sa) );
	sa.sin_family=AF_INET;
	socklen_t size=sizeof(sa);
	if( _sock>=0 ) getpeername( _sock,(sockaddr*)&sa,&size );
	address->Set( sa );
}

bool BBSocket::Bind( String host,int port ){

	sockaddr_in sa;
	setsockaddr( &sa,host,port );
	
	return bind( _sock,(sockaddr*)&sa,sizeof(sa) )>=0;
}

bool BBSocket::Connect( String host,int port ){

	sockaddr_in sa;
	setsockaddr( &sa,host,port );
	
	return connect( _sock,(sockaddr*)&sa,sizeof(sa) )>=0;
}

bool BBSocket::Listen( int backlog ){
	return listen( _sock,backlog )>=0;
}

bool BBSocket::Accept(){
	_accepted=accept( _sock,0,0 );
	return _accepted>=0;
}

BBSocket *BBSocket::Accepted(){
	if( _accepted>=0 ) return new BBSocket( _accepted );
	return 0;
}

int BBSocket::Send( BBDataBuffer *data,int offset,int count ){
	return send( _sock,(const char*)data->ReadPointer(offset),count,0 );
}

int BBSocket::Receive( BBDataBuffer *data,int offset,int count ){
	return recv( _sock,(char*)data->WritePointer( offset ),count,0 );
}

int BBSocket::SendTo( BBDataBuffer *data,int offset,int count,BBSocketAddress *address ){
	return sendto( _sock,(const char*)data->ReadPointer(offset),count,0,(sockaddr*)&address->_sa,sizeof(address->_sa) );
}

int BBSocket::ReceiveFrom( BBDataBuffer *data,int offset,int count,BBSocketAddress *address ){
	sockaddr_in sa;
	socklen_t size=sizeof(sa);
	memset( &sa,0,size );
	int n=recvfrom( _sock,(char*)data->WritePointer( offset ),count,0,(sockaddr*)&sa,&size );
	address->Set( sa );
	return n;
}

#endif


// The gloriously *MAD* winrt version!

#if WINDOWS_8

// ***** socket_winrt.h *****

#include <map>

using namespace Windows::Networking;
using namespace Windows::Networking::Sockets;
using namespace Windows::Storage::Streams;

class BBSocketAddress : public Object{
public:
	HostName ^hostname;
	Platform::String ^service;
	
	BBSocketAddress();

	void Set( BBSocketAddress *address );
	void Set( String host,int port );

	String Host();
	int Port();
	
	bool operator<( const BBSocketAddress &t )const;
};

class BBSocket : public Object{
public:

	enum{
		PROTOCOL_STREAM=1,
		PROTOCOL_SERVER=2,
		PROTOCOL_DATAGRAM=3
	};

	BBSocket();
	~BBSocket();
	
	bool Open( int protocol );
	bool Open( StreamSocket ^stream );
	void Close();
	
	bool Bind( String host,int port );
	bool Connect( String host,int port );
	bool Listen( int backlog );
	bool Accept();
	BBSocket *Accepted();

	int Send( BBDataBuffer *data,int offset,int count );
	int Receive( BBDataBuffer *data,int offset,int count );
	
	int SendTo( BBDataBuffer *data,int offset,int count,BBSocketAddress *address );
	int ReceiveFrom( BBDataBuffer *data,int offset,int count,BBSocketAddress *address );
	
	void GetLocalAddress( BBSocketAddress *address );
	void GetRemoteAddress( BBSocketAddress *address );

private:

	StreamSocket ^_stream;
	StreamSocketListener ^_server;
	DatagramSocket ^_datagram;
	
	HANDLE _revent;
	HANDLE _wevent;

	DataReader ^_reader;
	DataWriter ^_writer;
	
	AsyncOperationCompletedHandler<unsigned int> ^_recvhandler;
	AsyncOperationCompletedHandler<unsigned int> ^_sendhandler;
	AsyncOperationCompletedHandler<IOutputStream^> ^_getouthandler; 
	
	//for "server" sockets only
	StreamSocket ^_accepted;
	std::vector<StreamSocket^> _acceptQueue;
	int _acceptPut,_acceptGet;
	HANDLE _acceptSema;	

	//for "datagram" sockets only
	typedef DatagramSocketMessageReceivedEventArgs RecvArgs;
	std::vector<RecvArgs^> _recvQueue;
	int _recvPut,_recvGet;
	HANDLE _recvSema;
	
	//for "datagram" sendto
	std::map<BBSocketAddress,DataWriter^> _sendToMap;
	
	template<class X,class Y> struct Delegate{
		BBSocket *socket;
		void (BBSocket::*func)( X,Y );
		Delegate( BBSocket *socket,void (BBSocket::*func)( X,Y ) ):socket( socket ),func( func ){
		}
		void operator()( X x,Y y ){
			(socket->*func)( x,y );
		}
	};
	template<class X,class Y> friend struct Delegate;
	
	template<class X,class Y> Delegate<X,Y> MakeDelegate( void (BBSocket::*func)( X,Y ) ){
		return Delegate<X,Y>( this,func );
	}
	
	template<class X,class Y> TypedEventHandler<X,Y> ^CreateTypedEventHandler( void (BBSocket::*func)( X,Y ) ){
		return ref new TypedEventHandler<X,Y>( Delegate<X,Y>( this,func ) );
	}

	bool Wait( IAsyncAction ^action );
	
	void OnActionComplete( IAsyncAction ^action,AsyncStatus status );
	void OnSendComplete( IAsyncOperation<unsigned int> ^op,AsyncStatus status );
	void OnReceiveComplete( IAsyncOperation<unsigned int> ^op,AsyncStatus status );
	void OnConnectionReceived( StreamSocketListener ^listener,StreamSocketListenerConnectionReceivedEventArgs ^args );
	void OnMessageReceived( DatagramSocket ^socket,DatagramSocketMessageReceivedEventArgs ^args );
	void OnGetOutputStreamComplete( IAsyncOperation<IOutputStream^> ^op,AsyncStatus status );
};

// ***** socket_winrt.cpp *****

BBSocketAddress::BBSocketAddress():hostname( nullptr ),service( nullptr ){
}

void BBSocketAddress::Set( String host,int port ){
	HostName ^hostname=nullptr;
	if( host.Length() ) hostname=ref new HostName( host.ToWinRTString() );
	service=String( port ).ToWinRTString();
}

void BBSocketAddress::Set( BBSocketAddress *address ){
	hostname=address->hostname;
	service=address->service;
}

String BBSocketAddress::Host(){
	return hostname ? hostname->CanonicalName : "0.0.0.0";
}

int BBSocketAddress::Port(){
	return service ? String( service->Data(),service->Length() ).ToInt() : 0;
}

bool BBSocketAddress::operator<( const BBSocketAddress &t )const{
	if( hostname || t.hostname ){
		if( !hostname ) return true;
		if( !t.hostname ) return false;
		int n=HostName::Compare( hostname->CanonicalName,t.hostname->CanonicalName );
		if( n ) return n<0;
	}
	if( service || t.service ){
		if( !service ) return -1;
		if( !t.service ) return 1;
		int n=Platform::String::CompareOrdinal( service,t.service );
		if( n ) return n<0;
	}
	return false;
}

BBSocket::BBSocket(){

	_revent=CreateEventEx( 0,0,0,EVENT_ALL_ACCESS );
	_wevent=CreateEventEx( 0,0,0,EVENT_ALL_ACCESS );
	
	_recvSema=0;
	_acceptSema=0;
	
	_sendhandler=ref new AsyncOperationCompletedHandler<unsigned int>( MakeDelegate( &BBSocket::OnSendComplete ) );
	_recvhandler=ref new AsyncOperationCompletedHandler<unsigned int>( MakeDelegate( &BBSocket::OnReceiveComplete ) );
	_getouthandler=ref new AsyncOperationCompletedHandler<IOutputStream^>( MakeDelegate( &BBSocket::OnGetOutputStreamComplete ) );	
}

BBSocket::~BBSocket(){
	if( _revent ) CloseHandle( _revent );
	if( _wevent ) CloseHandle( _wevent );
	if( _recvSema ) CloseHandle( _recvSema );
	if( _acceptSema ) CloseHandle( _acceptSema );
}

void BBSocket::OnActionComplete( IAsyncAction ^action,AsyncStatus status ){
	SetEvent( _revent );
}

bool BBSocket::Wait( IAsyncAction ^action ){
	action->Completed=ref new AsyncActionCompletedHandler( MakeDelegate( &BBSocket::OnActionComplete ) );
	if( WaitForSingleObjectEx( _revent,INFINITE,FALSE )!=WAIT_OBJECT_0 ) return false;
	return action->Status==AsyncStatus::Completed;
}

bool BBSocket::Open( int protocol ){

	switch( protocol ){
	case PROTOCOL_STREAM:
		_stream=ref new StreamSocket();
		return true;
	case PROTOCOL_SERVER:
		_acceptGet=_acceptPut=0;
		_acceptQueue.resize( 256 );
		_acceptSema=CreateSemaphoreEx( 0,0,256,0,0,EVENT_ALL_ACCESS );
		_server=ref new StreamSocketListener();
		_server->ConnectionReceived+=CreateTypedEventHandler( &BBSocket::OnConnectionReceived );
		return true;
	case PROTOCOL_DATAGRAM:
		_recvGet=_recvPut=0;
		_recvQueue.resize( 256 );
		_recvSema=CreateSemaphoreEx( 0,0,256,0,0,EVENT_ALL_ACCESS );
		_datagram=ref new DatagramSocket();
		_datagram->MessageReceived+=CreateTypedEventHandler( &BBSocket::OnMessageReceived );
		return true;
	}

	return false;
}

bool BBSocket::Open( StreamSocket ^stream ){

	_stream=stream;
	
	_reader=ref new DataReader( _stream->InputStream );
	_reader->InputStreamOptions=InputStreamOptions::Partial;
	
	_writer=ref new DataWriter( _stream->OutputStream );
	
	return true;
}

void BBSocket::Close(){
	if( _stream ) delete _stream;
	if( _server ) delete _server;
	if( _datagram ) delete _datagram;
	_stream=nullptr;
	_server=nullptr;
	_datagram=nullptr;
}

bool BBSocket::Bind( String host,int port ){

	HostName ^hostname=nullptr;
	if( host.Length() ) hostname=ref new HostName( host.ToWinRTString() );
	auto service=(port ? String( port ) : String()).ToWinRTString();

	if( _stream ){
//		return Wait( _stream->BindEndpointAsync( hostname,service ) );
	}else if( _server ){
		return Wait( _server->BindEndpointAsync( hostname,service ) );
	}else if( _datagram ){
		return Wait( _datagram->BindEndpointAsync( hostname,service ) );
	}

	return false;
}

bool BBSocket::Listen( int backlog ){
	return _server!=nullptr;
}

bool BBSocket::Accept(){
	if( WaitForSingleObjectEx( _acceptSema,INFINITE,FALSE )!=WAIT_OBJECT_0 ) return false;
	_accepted=_acceptQueue[_acceptGet & 255];
	_acceptQueue[_acceptGet++ & 255]=nullptr;
	return true;
}

BBSocket *BBSocket::Accepted(){
	BBSocket *socket=new BBSocket();
	if( socket->Open( _accepted ) ) return socket;
	return 0;
}

void BBSocket::OnConnectionReceived( StreamSocketListener ^listener,StreamSocketListenerConnectionReceivedEventArgs ^args ){

	_acceptQueue[_acceptPut++ & 255]=args->Socket;
	ReleaseSemaphore( _acceptSema,1,0 );
}

void BBSocket::OnMessageReceived( DatagramSocket ^socket,DatagramSocketMessageReceivedEventArgs ^args ){

	_recvQueue[_recvPut++ & 255]=args;
	ReleaseSemaphore( _recvSema,1,0 );
}

bool BBSocket::Connect( String host,int port ){

	auto hostname=ref new HostName( host.ToWinRTString() );
	auto service=String( port ).ToWinRTString();

	if( _stream ){

		if( !Wait( _stream->ConnectAsync( hostname,service ) ) ) return false;
		
		_reader=ref new DataReader( _stream->InputStream );
		_reader->InputStreamOptions=InputStreamOptions::Partial;

		_writer=ref new DataWriter( _stream->OutputStream );
	
		return true;
		
	}else if( _datagram ) {
	
		if( !Wait( _datagram->ConnectAsync( hostname,service ) ) ) return false;
		
		_writer=ref new DataWriter( _datagram->OutputStream );
		
		return true;
	}
}

int BBSocket::Send( BBDataBuffer *data,int offset,int count ){

	if( !_writer ) return 0;

	const unsigned char *p=(const unsigned char*)data->ReadPointer( offset );
	
	_writer->WriteBytes( Platform::ArrayReference<uint8>( (uint8*)p,count ) );
	auto op=_writer->StoreAsync();
	op->Completed=_sendhandler;
	
	if( WaitForSingleObjectEx( _wevent,INFINITE,FALSE )!=WAIT_OBJECT_0 ) return 0;

//	if( op->Status!=AsyncStatus::Completed ) return 0;
	
	return count;
}

void BBSocket::OnSendComplete( IAsyncOperation<unsigned int> ^op,AsyncStatus status ){

	SetEvent( _wevent );
}

int BBSocket::Receive( BBDataBuffer *data,int offset,int count ){

	if( _stream ){
	
		auto op=_reader->LoadAsync( count );
		op->Completed=_recvhandler;
	
		if( WaitForSingleObjectEx( _revent,INFINITE,FALSE )!=WAIT_OBJECT_0 ) return 0;
		
	//	if( op->Status!=AsyncStatus::Completed ) return 0;
		
		int n=_reader->UnconsumedBufferLength;
			
		_reader->ReadBytes( Platform::ArrayReference<uint8>( (uint8*)data->WritePointer( offset ),n ) );
	
		return n;
		
	}else if( _datagram ){

		if( WaitForSingleObjectEx( _recvSema,INFINITE,FALSE )!=WAIT_OBJECT_0 ) return 0;
		
		auto recvArgs=_recvQueue[_recvGet & 255];
		_recvQueue[_recvGet++ & 255]=nullptr;
		
		auto reader=recvArgs->GetDataReader();
		int n=reader->UnconsumedBufferLength;
		if( n>count ) n=count;

		reader->ReadBytes( Platform::ArrayReference<uint8>( (uint8*)data->WritePointer( offset ),n ) );
		
		return n;
	}
	return 0;
}

void BBSocket::OnReceiveComplete( IAsyncOperation<unsigned int> ^op,AsyncStatus status ){

	SetEvent( _revent );
}

int BBSocket::SendTo( BBDataBuffer *data,int offset,int count,BBSocketAddress *address ){

	auto it=_sendToMap.find( *address );
	
	if( it==_sendToMap.end() ){
	
		auto op=_datagram->GetOutputStreamAsync( address->hostname,address->service );
		op->Completed=_getouthandler;
		
		if( WaitForSingleObjectEx( _wevent,INFINITE,FALSE )!=WAIT_OBJECT_0 || op->Status!=AsyncStatus::Completed ){
			bbPrint( "GetOutputStream failed" );
			return 0;
		}	
		it=_sendToMap.insert( std::make_pair( *address,ref new DataWriter( op->GetResults() ) ) ).first;
	}

	auto writer=it->second;

	writer->WriteBytes( Platform::ArrayReference<uint8>( (uint8*)data->ReadPointer( offset ),count ) );
	auto op=writer->StoreAsync();
	op->Completed=_sendhandler;
	
	if( WaitForSingleObjectEx( _wevent,INFINITE,FALSE )!=WAIT_OBJECT_0 ) return 0;

//	if( op->Status!=AsyncStatus::Completed ) return 0;
	
	return count;
}

void BBSocket::OnGetOutputStreamComplete( IAsyncOperation<IOutputStream^> ^op,AsyncStatus status ){

	SetEvent( _wevent );
}

int BBSocket::ReceiveFrom( BBDataBuffer *data,int offset,int count,BBSocketAddress *address ){

	if( !_datagram ) return 0;
	
	if( WaitForSingleObjectEx( _recvSema,INFINITE,FALSE )!=WAIT_OBJECT_0 ) return 0;
	
	auto recvArgs=_recvQueue[_recvGet & 255];
	_recvQueue[_recvGet++ & 255]=nullptr;
	
	auto reader=recvArgs->GetDataReader();
	int n=reader->UnconsumedBufferLength;
	if( n>count ) n=count;

	reader->ReadBytes( Platform::ArrayReference<uint8>( (uint8*)data->WritePointer( offset ),n ) );

	address->hostname=recvArgs->RemoteAddress;
	address->service=recvArgs->RemotePort;
	
	return n;
}
	
void BBSocket::GetLocalAddress( BBSocketAddress *address ){
	if( _stream ){
		address->hostname=_stream->Information->LocalAddress;
		address->service=_stream->Information->LocalPort;
	}else if( _server ){
		address->hostname=nullptr;
		address->service=_server->Information->LocalPort;
	}else if( _datagram ){
		address->hostname=_datagram->Information->LocalAddress;
		address->service=_datagram->Information->LocalPort;
	}
}

void BBSocket::GetRemoteAddress( BBSocketAddress *address ){
	if( _stream ){
		address->hostname=_stream->Information->RemoteAddress;
		address->service=_stream->Information->RemotePort;
	}else if( _server ){
		address->hostname=nullptr;
		address->service=nullptr;
	}else if( _datagram ){
		address->hostname=_datagram->Information->RemoteAddress;
		address->service=_datagram->Information->RemotePort;
	}
}

#endif

class c_App;
class c_Game_app;
class c_GameDelegate;
class c_Image;
class c_GraphicsContext;
class c_Frame;
class c_InputDevice;
class c_JoyState;
class c_DisplayMode;
class c_Map;
class c_IntMap;
class c_Stack;
class c_Node;
class c_BBGameEvent;
class c_Player;
class c_RayGunBullet;
class c_List;
class c_Node2;
class c_HeadNode;
class c_Zombie;
class c_List2;
class c_Node3;
class c_HeadNode2;
class c_Sound;
class c_TFont;
class c_DataStream;
class c_DataBuffer;
class c_TFont_Glyph;
class c_Stack2;
class c_TFont_Poly;
class c_FloatStack;
class c_Stack3;
class c_Stream;
class c_FileStream;
class c_List3;
class c_IntList;
class c_Node4;
class c_HeadNode3;
class c_StreamError;
class c_StreamReadError;
class c_Stack4;
class c_Enumerator;
class c_Enumerator2;
class c_StreamWriteError;
class c_Enumerator3;
class c_Enumerator4;
class c_App : public Object{
	public:
	c_App();
	c_App* m_new();
	int p_OnResize();
	virtual int p_OnCreate();
	int p_OnSuspend();
	int p_OnResume();
	virtual int p_OnUpdate();
	int p_OnLoading();
	virtual int p_OnRender();
	int p_OnClose();
	int p_OnBack();
	void mark();
	String debug();
};
String dbg_type(c_App**p){return "App";}
class c_Game_app : public c_App{
	public:
	c_Player* m_player;
	c_List* m_raygunbullet_collection;
	c_List2* m_enemy_collection;
	c_Image* m_menu;
	c_Image* m_help;
	c_Image* m_help2;
	c_Image* m_help3;
	c_Image* m_helpoverlay;
	c_Image* m_room;
	c_Image* m_endscreen;
	c_Image* m_leaderboard;
	int m_helpscreen;
	int m_tick;
	int m_tick_health;
	int m_tick_reload;
	bool m_regenerate;
	int m_score;
	int m_points;
	int m_zombies_killed_in_round;
	int m_zombies_in_round;
	int m_zombies_spawned_in_round;
	int m_round;
	String m_reload_state;
	int m_reload_cycle;
	int m_health;
	int m_raygunbullet_count;
	bool m_enemy_collision;
	bool m_player_collision;
	c_Sound* m_reload_sound;
	c_Sound* m_shoot_sound;
	c_Sound* m_newRound_sound;
	c_Sound* m_punch_sound;
	c_Sound* m_knife_sound;
	c_Sound* m_click_sound;
	bool m_direction;
	Float m_music_volume;
	String m_music_track;
	int m_health_zombie;
	int m_gun_damage;
	int m_PaP_upgrades;
	int m_weapon_damage;
	int m_tick_PaP;
	bool m_weapon_upgradeCooldown;
	int m_PaP_cost;
	int m_PaP_cycle;
	int m_spawned_on_screen;
	String m_name;
	String m_name_given;
	bool m_pressed_p;
	c_Image* m_settings;
	String m_path_given;
	c_Game_app();
	c_Game_app* m_new();
	static String m_GameState;
	int p_OnCreate();
	int p_OnUpdate();
	int p_OnRender();
	void mark();
	String debug();
};
String dbg_type(c_Game_app**p){return "Game_app";}
extern c_App* bb_app__app;
class c_GameDelegate : public BBGameDelegate{
	public:
	gxtkGraphics* m__graphics;
	gxtkAudio* m__audio;
	c_InputDevice* m__input;
	c_GameDelegate();
	c_GameDelegate* m_new();
	void StartGame();
	void SuspendGame();
	void ResumeGame();
	void UpdateGame();
	void RenderGame();
	void KeyEvent(int,int);
	void MouseEvent(int,int,Float,Float);
	void TouchEvent(int,int,Float,Float);
	void MotionEvent(int,int,Float,Float,Float);
	void DiscardGraphics();
	void mark();
	String debug();
};
String dbg_type(c_GameDelegate**p){return "GameDelegate";}
extern c_GameDelegate* bb_app__delegate;
extern BBGame* bb_app__game;
extern c_Game_app* bb_shooter_Game;
int bbMain();
extern gxtkGraphics* bb_graphics_device;
int bb_graphics_SetGraphicsDevice(gxtkGraphics*);
class c_Image : public Object{
	public:
	gxtkSurface* m_surface;
	int m_width;
	int m_height;
	Array<c_Frame* > m_frames;
	int m_flags;
	Float m_tx;
	Float m_ty;
	c_Image* m_source;
	c_Image();
	static int m_DefaultFlags;
	c_Image* m_new();
	int p_SetHandle(Float,Float);
	int p_ApplyFlags(int);
	c_Image* p_Init(gxtkSurface*,int,int);
	c_Image* p_Init2(gxtkSurface*,int,int,int,int,int,int,c_Image*,int,int,int,int);
	int p_WritePixels(Array<int >,int,int,int,int,int,int);
	int p_Width();
	int p_Height();
	int p_Frames();
	void mark();
	String debug();
};
String dbg_type(c_Image**p){return "Image";}
class c_GraphicsContext : public Object{
	public:
	c_Image* m_defaultFont;
	c_Image* m_font;
	int m_firstChar;
	int m_matrixSp;
	Float m_ix;
	Float m_iy;
	Float m_jx;
	Float m_jy;
	Float m_tx;
	Float m_ty;
	int m_tformed;
	int m_matDirty;
	Float m_color_r;
	Float m_color_g;
	Float m_color_b;
	Float m_alpha;
	int m_blend;
	Float m_scissor_x;
	Float m_scissor_y;
	Float m_scissor_width;
	Float m_scissor_height;
	Array<Float > m_matrixStack;
	c_GraphicsContext();
	c_GraphicsContext* m_new();
	int p_Validate();
	void mark();
	String debug();
};
String dbg_type(c_GraphicsContext**p){return "GraphicsContext";}
extern c_GraphicsContext* bb_graphics_context;
String bb_data_FixDataPath(String);
class c_Frame : public Object{
	public:
	int m_x;
	int m_y;
	c_Frame();
	c_Frame* m_new(int,int);
	c_Frame* m_new2();
	void mark();
	String debug();
};
String dbg_type(c_Frame**p){return "Frame";}
c_Image* bb_graphics_LoadImage(String,int,int);
c_Image* bb_graphics_LoadImage2(String,int,int,int,int);
int bb_graphics_SetFont(c_Image*,int);
extern gxtkAudio* bb_audio_device;
int bb_audio_SetAudioDevice(gxtkAudio*);
class c_InputDevice : public Object{
	public:
	Array<c_JoyState* > m__joyStates;
	Array<bool > m__keyDown;
	int m__keyHitPut;
	Array<int > m__keyHitQueue;
	Array<int > m__keyHit;
	int m__charGet;
	int m__charPut;
	Array<int > m__charQueue;
	Float m__mouseX;
	Float m__mouseY;
	Array<Float > m__touchX;
	Array<Float > m__touchY;
	Float m__accelX;
	Float m__accelY;
	Float m__accelZ;
	c_InputDevice();
	c_InputDevice* m_new();
	void p_PutKeyHit(int);
	void p_BeginUpdate();
	void p_EndUpdate();
	void p_KeyEvent(int,int);
	void p_MouseEvent(int,int,Float,Float);
	void p_TouchEvent(int,int,Float,Float);
	void p_MotionEvent(int,int,Float,Float,Float);
	int p_KeyHit(int);
	int p_GetChar();
	bool p_KeyDown(int);
	void mark();
	String debug();
};
String dbg_type(c_InputDevice**p){return "InputDevice";}
class c_JoyState : public Object{
	public:
	Array<Float > m_joyx;
	Array<Float > m_joyy;
	Array<Float > m_joyz;
	Array<bool > m_buttons;
	c_JoyState();
	c_JoyState* m_new();
	void mark();
	String debug();
};
String dbg_type(c_JoyState**p){return "JoyState";}
extern c_InputDevice* bb_input_device;
int bb_input_SetInputDevice(c_InputDevice*);
extern int bb_app__devWidth;
extern int bb_app__devHeight;
void bb_app_ValidateDeviceWindow(bool);
class c_DisplayMode : public Object{
	public:
	int m__width;
	int m__height;
	c_DisplayMode();
	c_DisplayMode* m_new(int,int);
	c_DisplayMode* m_new2();
	void mark();
	String debug();
};
String dbg_type(c_DisplayMode**p){return "DisplayMode";}
class c_Map : public Object{
	public:
	c_Node* m_root;
	c_Map();
	c_Map* m_new();
	virtual int p_Compare(int,int)=0;
	c_Node* p_FindNode(int);
	bool p_Contains(int);
	int p_RotateLeft(c_Node*);
	int p_RotateRight(c_Node*);
	int p_InsertFixup(c_Node*);
	bool p_Set(int,c_DisplayMode*);
	bool p_Insert(int,c_DisplayMode*);
	void mark();
	String debug();
};
String dbg_type(c_Map**p){return "Map";}
class c_IntMap : public c_Map{
	public:
	c_IntMap();
	c_IntMap* m_new();
	int p_Compare(int,int);
	void mark();
	String debug();
};
String dbg_type(c_IntMap**p){return "IntMap";}
class c_Stack : public Object{
	public:
	Array<c_DisplayMode* > m_data;
	int m_length;
	c_Stack();
	c_Stack* m_new();
	c_Stack* m_new2(Array<c_DisplayMode* >);
	void p_Push(c_DisplayMode*);
	void p_Push2(Array<c_DisplayMode* >,int,int);
	void p_Push3(Array<c_DisplayMode* >,int);
	Array<c_DisplayMode* > p_ToArray();
	void mark();
	String debug();
};
String dbg_type(c_Stack**p){return "Stack";}
class c_Node : public Object{
	public:
	int m_key;
	c_Node* m_right;
	c_Node* m_left;
	c_DisplayMode* m_value;
	int m_color;
	c_Node* m_parent;
	c_Node();
	c_Node* m_new(int,c_DisplayMode*,int,c_Node*);
	c_Node* m_new2();
	void mark();
	String debug();
};
String dbg_type(c_Node**p){return "Node";}
extern Array<c_DisplayMode* > bb_app__displayModes;
extern c_DisplayMode* bb_app__desktopMode;
int bb_app_DeviceWidth();
int bb_app_DeviceHeight();
void bb_app_EnumDisplayModes();
extern gxtkGraphics* bb_graphics_renderDevice;
int bb_graphics_SetMatrix(Float,Float,Float,Float,Float,Float);
int bb_graphics_SetMatrix2(Array<Float >);
int bb_graphics_SetColor(Float,Float,Float);
int bb_graphics_SetAlpha(Float);
int bb_graphics_SetBlend(int);
int bb_graphics_SetScissor(Float,Float,Float,Float);
int bb_graphics_BeginRender();
int bb_graphics_EndRender();
class c_BBGameEvent : public Object{
	public:
	c_BBGameEvent();
	void mark();
	String debug();
};
String dbg_type(c_BBGameEvent**p){return "BBGameEvent";}
void bb_app_EndApp();
class c_Player : public Object{
	public:
	Float m_x;
	Float m_y;
	int m_sprint_energy;
	Float m_Player_speed;
	Float m_sprint_multiplier;
	bool m_up;
	bool m_down;
	bool m_left;
	bool m_right;
	Float m_yspeed;
	Float m_xspeed;
	Float m_x_old;
	Float m_y_old;
	bool m_sprint_regeneration;
	int m_tick_sprint;
	Float m_increm;
	Float m_x_disp;
	Float m_y_disp;
	Float m_resultant_disp;
	Float m_travel;
	c_Sound* m_footstep2_sound;
	c_Sound* m_huff_sound;
	c_Image* m_sprite;
	c_Image* m_sprite2;
	c_Player();
	c_Player* m_new();
	int p_Move(String,int);
	int p_give_x();
	int p_give_y();
	int p_give_sprint_energy();
	void mark();
	String debug();
};
String dbg_type(c_Player**p){return "Player";}
class c_RayGunBullet : public Object{
	public:
	Float m_x;
	Float m_y;
	bool m_up;
	bool m_down;
	bool m_left;
	bool m_right;
	int m_RayGunBullet_speed;
	int m_zombies_hit;
	c_Image* m_sprite;
	c_RayGunBullet();
	c_RayGunBullet* m_new(Float,Float);
	c_RayGunBullet* m_new2();
	int p_Move2(String);
	int p_health(int);
	void mark();
	String debug();
};
String dbg_type(c_RayGunBullet**p){return "RayGunBullet";}
class c_List : public Object{
	public:
	c_Node2* m__head;
	c_List();
	c_List* m_new();
	c_Node2* p_AddLast(c_RayGunBullet*);
	c_List* m_new2(Array<c_RayGunBullet* >);
	c_Enumerator3* p_ObjectEnumerator();
	bool p_Equals(c_RayGunBullet*,c_RayGunBullet*);
	int p_RemoveEach(c_RayGunBullet*);
	void p_Remove(c_RayGunBullet*);
	void mark();
	String debug();
};
String dbg_type(c_List**p){return "List";}
class c_Node2 : public Object{
	public:
	c_Node2* m__succ;
	c_Node2* m__pred;
	c_RayGunBullet* m__data;
	c_Node2();
	c_Node2* m_new(c_Node2*,c_Node2*,c_RayGunBullet*);
	c_Node2* m_new2();
	int p_Remove2();
	void mark();
	String debug();
};
String dbg_type(c_Node2**p){return "Node";}
class c_HeadNode : public c_Node2{
	public:
	c_HeadNode();
	c_HeadNode* m_new();
	void mark();
	String debug();
};
String dbg_type(c_HeadNode**p){return "HeadNode";}
class c_Zombie : public Object{
	public:
	Float m_x_Rnd;
	Float m_y_Rnd;
	Float m_x;
	Float m_y;
	Float m_Zombie_speed;
	Float m_angle;
	bool m_player_advantage;
	int m_tick_tap;
	int m_damage_taken;
	c_Image* m_sprite;
	c_Zombie();
	c_Zombie* m_new();
	int p_Move3(Float,Float,String,bool,bool,int);
	int p_dead(int,int);
	void mark();
	String debug();
};
String dbg_type(c_Zombie**p){return "Zombie";}
class c_List2 : public Object{
	public:
	c_Node3* m__head;
	c_List2();
	c_List2* m_new();
	c_Node3* p_AddLast2(c_Zombie*);
	c_List2* m_new2(Array<c_Zombie* >);
	c_Enumerator4* p_ObjectEnumerator();
	bool p_Equals2(c_Zombie*,c_Zombie*);
	int p_RemoveEach2(c_Zombie*);
	void p_Remove3(c_Zombie*);
	void mark();
	String debug();
};
String dbg_type(c_List2**p){return "List";}
class c_Node3 : public Object{
	public:
	c_Node3* m__succ;
	c_Node3* m__pred;
	c_Zombie* m__data;
	c_Node3();
	c_Node3* m_new(c_Node3*,c_Node3*,c_Zombie*);
	c_Node3* m_new2();
	int p_Remove2();
	void mark();
	String debug();
};
String dbg_type(c_Node3**p){return "Node";}
class c_HeadNode2 : public c_Node3{
	public:
	c_HeadNode2();
	c_HeadNode2* m_new();
	void mark();
	String debug();
};
String dbg_type(c_HeadNode2**p){return "HeadNode";}
extern int bb_random_Seed;
Float bb_random_Rnd();
Float bb_random_Rnd2(Float,Float);
Float bb_random_Rnd3(Float);
extern int bb_app__updateRate;
void bb_app_SetUpdateRate(int);
class c_Sound : public Object{
	public:
	gxtkSample* m_sample;
	c_Sound();
	c_Sound* m_new(gxtkSample*);
	c_Sound* m_new2();
	void mark();
	String debug();
};
String dbg_type(c_Sound**p){return "Sound";}
c_Sound* bb_audio_LoadSound(String);
class c_TFont : public Object{
	public:
	c_DataStream* m_Stream;
	int m_Size;
	Array<int > m_Color;
	String m_Path;
	String m_OutlineType;
	int m_GlyphNumber;
	Array<int > m_FontLimits;
	Float m_FontScale;
	int m_LineHeight;
	Array<int > m_GlyphId;
	Array<c_TFont_Glyph* > m_Glyph;
	bool m_ClockwiseFound;
	bool m_FontClockwise;
	bool m_ImagesLoaded;
	c_TFont();
	int p_LoadMetrics(int,int);
	int p_LoadCmapTable0(int);
	int p_LoadCmapTable4(int);
	int p_LoadCmapTable6(int);
	int p_LoadCmapData(int);
	int p_LoadLoca(int,int,int);
	int p_LoadGlyfData(int,int);
	int p_QuadGlyph(int);
	Array<Float > p_CalculateCurve(int,int,int,int,int,int);
	int p_SmoothGlyph(int);
	c_TFont* m_new(String,int,Array<int >);
	c_TFont* m_new2();
	int p_LoadGlyphImages();
	int p_TextHeight(String,int);
	int p_TextWidth(String,int);
	int p_DrawText(String,int,int,int,int,int,int);
	void mark();
	String debug();
};
String dbg_type(c_TFont**p){return "TFont";}
class c_DataStream : public Object{
	public:
	c_DataBuffer* m_Buffer;
	int m_Pointer;
	bool m_BigEndian;
	c_DataStream();
	c_DataStream* m_new(String,bool);
	c_DataStream* m_new2();
	Array<int > p_ByteToArr(int);
	Array<int > p_BytesToArr(int,int);
	static Array<int > m_ChangeEndian(Array<int >);
	static int m_CalculateBits(Array<int >);
	int p_ReadInt(int);
	Float p_ReadFixed32();
	static int m_CalculateUBits(Array<int >);
	int p_ReadUInt(int);
	String p_ReadString(int);
	int p_PeekUInt(int,int);
	int p_PeekInt(int,int);
	int p_SetPointer(int);
	Array<int > p_ReadBits(int);
	void mark();
	String debug();
};
String dbg_type(c_DataStream**p){return "DataStream";}
class c_DataBuffer : public BBDataBuffer{
	public:
	c_DataBuffer();
	c_DataBuffer* m_new(int);
	c_DataBuffer* m_new2();
	static c_DataBuffer* m_Load(String);
	void p_PeekBytes(int,Array<int >,int,int);
	Array<int > p_PeekBytes2(int,int);
	String p_PeekString(int,int,String);
	String p_PeekString2(int,String);
	void p_CopyBytes(int,c_DataBuffer*,int,int);
	void p_PokeBytes(int,Array<int >,int,int);
	int p_PokeString(int,String,String);
	void mark();
	String debug();
};
String dbg_type(c_DataBuffer**p){return "DataBuffer";}
class c_TFont_Glyph : public Object{
	public:
	int m_Adv;
	int m_Lsb;
	int m_FileAddress;
	int m_ContourNumber;
	int m_xMin;
	int m_yMin;
	int m_xMax;
	int m_yMax;
	int m_W;
	int m_H;
	Array<Array<Float > > m_xyList;
	Array<c_TFont_Poly* > m_Poly;
	c_Image* m_Img;
	c_TFont_Glyph();
	c_TFont_Glyph* m_new();
	void mark();
	String debug();
};
String dbg_type(c_TFont_Glyph**p){return "TFont_Glyph";}
class c_Stack2 : public Object{
	public:
	Array<Float > m_data;
	int m_length;
	c_Stack2();
	c_Stack2* m_new();
	c_Stack2* m_new2(Array<Float >);
	void p_Push4(Float);
	void p_Push5(Array<Float >,int,int);
	void p_Push6(Array<Float >,int);
	Array<Float > p_ToArray();
	static Float m_NIL;
	void p_Clear();
	void p_Length(int);
	int p_Length2();
	Float p_Get(int);
	void p_Remove4(int);
	void mark();
	String debug();
};
String dbg_type(c_Stack2**p){return "Stack";}
class c_TFont_Poly : public Object{
	public:
	Float m_Scaler;
	Array<Float > m_OrigArray;
	Array<Float > m_xyList;
	bool m_Clockwise;
	c_Stack3* m_Triangles;
	c_TFont_Poly();
	bool p_GetClockWise();
	static c_FloatStack* m_points;
	void p_BuildPoints();
	Float p_Heading(Float,Float,Float,Float);
	Float p_Angle(int);
	Float p_DotProduct(int,int,int);
	bool p_AnyInside(int,int,int);
	int p_Triangulate();
	c_TFont_Poly* m_new(Array<Float >,Float);
	c_TFont_Poly* m_new2();
	int p_Draw();
	void mark();
	String debug();
};
String dbg_type(c_TFont_Poly**p){return "TFont_Poly";}
class c_FloatStack : public c_Stack2{
	public:
	c_FloatStack();
	c_FloatStack* m_new(Array<Float >);
	c_FloatStack* m_new2();
	void mark();
	String debug();
};
String dbg_type(c_FloatStack**p){return "FloatStack";}
int bb_math_Max(int,int);
Float bb_math_Max2(Float,Float);
class c_Stack3 : public Object{
	public:
	Array<Array<Float > > m_data;
	int m_length;
	c_Stack3();
	c_Stack3* m_new();
	c_Stack3* m_new2(Array<Array<Float > >);
	void p_Push7(Array<Float >);
	void p_Push8(Array<Array<Float > >,int,int);
	void p_Push9(Array<Array<Float > >,int);
	static Array<Float > m_NIL;
	void p_Length(int);
	int p_Length2();
	Array<Float > p_Get(int);
	void mark();
	String debug();
};
String dbg_type(c_Stack3**p){return "Stack";}
extern c_TFont* bb_shooter_lbfont;
int bb_audio_PlayMusic(String,int);
int bb_audio_ResumeMusic();
int bb_input_KeyHit(int);
int bb_audio_PauseMusic();
class c_Stream : public Object{
	public:
	c_Stream();
	c_Stream* m_new();
	virtual int p_Read(c_DataBuffer*,int,int)=0;
	void p_ReadError();
	void p_ReadAll(c_DataBuffer*,int,int);
	c_DataBuffer* p_ReadAll2();
	String p_ReadString2(int,String);
	String p_ReadString3(String);
	virtual void p_Close()=0;
	virtual int p_Write(c_DataBuffer*,int,int)=0;
	void p_WriteError();
	void p_WriteAll(c_DataBuffer*,int,int);
	void p_WriteString(String,String);
	void mark();
	String debug();
};
String dbg_type(c_Stream**p){return "Stream";}
class c_FileStream : public c_Stream{
	public:
	BBFileStream* m__stream;
	c_FileStream();
	static BBFileStream* m_OpenStream(String,String);
	c_FileStream* m_new(String,String);
	c_FileStream* m_new2(BBFileStream*);
	c_FileStream* m_new3();
	static c_FileStream* m_Open(String,String);
	void p_Close();
	int p_Read(c_DataBuffer*,int,int);
	int p_Write(c_DataBuffer*,int,int);
	void mark();
	String debug();
};
String dbg_type(c_FileStream**p){return "FileStream";}
class c_List3 : public Object{
	public:
	c_Node4* m__head;
	c_List3();
	c_List3* m_new();
	c_Node4* p_AddLast3(int);
	c_List3* m_new2(Array<int >);
	virtual int p_Compare(int,int);
	int p_Sort(int);
	c_Enumerator2* p_ObjectEnumerator();
	void mark();
	String debug();
};
String dbg_type(c_List3**p){return "List";}
class c_IntList : public c_List3{
	public:
	c_IntList();
	c_IntList* m_new(Array<int >);
	c_IntList* m_new2();
	int p_Compare(int,int);
	void mark();
	String debug();
};
String dbg_type(c_IntList**p){return "IntList";}
class c_Node4 : public Object{
	public:
	c_Node4* m__succ;
	c_Node4* m__pred;
	int m__data;
	c_Node4();
	c_Node4* m_new(c_Node4*,c_Node4*,int);
	c_Node4* m_new2();
	void mark();
	String debug();
};
String dbg_type(c_Node4**p){return "Node";}
class c_HeadNode3 : public c_Node4{
	public:
	c_HeadNode3();
	c_HeadNode3* m_new();
	void mark();
	String debug();
};
String dbg_type(c_HeadNode3**p){return "HeadNode";}
extern String bb_shooter_path;
class c_StreamError : public ThrowableObject{
	public:
	c_Stream* m__stream;
	c_StreamError();
	c_StreamError* m_new(c_Stream*);
	c_StreamError* m_new2();
	void mark();
	String debug();
};
String dbg_type(c_StreamError**p){return "StreamError";}
class c_StreamReadError : public c_StreamError{
	public:
	c_StreamReadError();
	c_StreamReadError* m_new(c_Stream*);
	c_StreamReadError* m_new2();
	void mark();
	String debug();
};
String dbg_type(c_StreamReadError**p){return "StreamReadError";}
class c_Stack4 : public Object{
	public:
	Array<c_DataBuffer* > m_data;
	int m_length;
	c_Stack4();
	c_Stack4* m_new();
	c_Stack4* m_new2(Array<c_DataBuffer* >);
	void p_Push10(c_DataBuffer*);
	void p_Push11(Array<c_DataBuffer* >,int,int);
	void p_Push12(Array<c_DataBuffer* >,int);
	c_Enumerator* p_ObjectEnumerator();
	static c_DataBuffer* m_NIL;
	void p_Length(int);
	int p_Length2();
	void mark();
	String debug();
};
String dbg_type(c_Stack4**p){return "Stack";}
class c_Enumerator : public Object{
	public:
	c_Stack4* m_stack;
	int m_index;
	c_Enumerator();
	c_Enumerator* m_new(c_Stack4*);
	c_Enumerator* m_new2();
	bool p_HasNext();
	c_DataBuffer* p_NextObject();
	void mark();
	String debug();
};
String dbg_type(c_Enumerator**p){return "Enumerator";}
class c_Enumerator2 : public Object{
	public:
	c_List3* m__list;
	c_Node4* m__curr;
	c_Enumerator2();
	c_Enumerator2* m_new(c_List3*);
	c_Enumerator2* m_new2();
	bool p_HasNext();
	int p_NextObject();
	void mark();
	String debug();
};
String dbg_type(c_Enumerator2**p){return "Enumerator";}
extern Array<int > bb_shooter_playerscores;
extern Array<String > bb_shooter_names;
void bb_shooter_InsertionSort();
int bb_shooter_toptenplayers();
int bb_audio_SetMusicVolume(Float);
int bb_input_GetChar();
class c_StreamWriteError : public c_StreamError{
	public:
	c_StreamWriteError();
	c_StreamWriteError* m_new(c_Stream*);
	c_StreamWriteError* m_new2();
	void mark();
	String debug();
};
String dbg_type(c_StreamWriteError**p){return "StreamWriteError";}
int bb_shooter_writenamesandscore(String,int);
int bb_audio_StopMusic();
int bb_audio_PlaySound(c_Sound*,int,int);
int bb_input_KeyDown(int);
class c_Enumerator3 : public Object{
	public:
	c_List* m__list;
	c_Node2* m__curr;
	c_Enumerator3();
	c_Enumerator3* m_new(c_List*);
	c_Enumerator3* m_new2();
	bool p_HasNext();
	c_RayGunBullet* p_NextObject();
	void mark();
	String debug();
};
String dbg_type(c_Enumerator3**p){return "Enumerator";}
class c_Enumerator4 : public Object{
	public:
	c_List2* m__list;
	c_Node3* m__curr;
	c_Enumerator4();
	c_Enumerator4* m_new(c_List2*);
	c_Enumerator4* m_new2();
	bool p_HasNext();
	c_Zombie* p_NextObject();
	void mark();
	String debug();
};
String dbg_type(c_Enumerator4**p){return "Enumerator";}
bool bb_shooter_intersects(int,int,int,int,int,int,int,int);
int bb_graphics_DebugRenderDevice();
int bb_graphics_DrawImage(c_Image*,Float,Float,int);
int bb_graphics_PushMatrix();
int bb_graphics_Transform(Float,Float,Float,Float,Float,Float);
int bb_graphics_Transform2(Array<Float >);
int bb_graphics_Translate(Float,Float);
int bb_graphics_Rotate(Float);
int bb_graphics_Scale(Float,Float);
int bb_graphics_PopMatrix();
int bb_graphics_DrawImage2(c_Image*,Float,Float,Float,Float,Float,int);
int bb_graphics_Cls(Float,Float,Float);
Array<Float > bb_graphics_GetColor();
int bb_graphics_GetColor2(Array<Float >);
c_Image* bb_graphics_CreateImage(int,int,int,int);
int bb_graphics_ReadPixels(Array<int >,int,int,int,int,int,int);
int bb_graphics_DrawRect(Float,Float,Float,Float);
int bb_graphics_DrawPoly(Array<Float >);
int bb_graphics_DrawPoly2(Array<Float >,c_Image*,int);
int bb_graphics_DrawText(String,Float,Float,Float,Float);
void gc_mark( BBGame *p ){}
String dbg_type( BBGame **p ){ return "BBGame"; }
String dbg_value( BBGame **p ){ return dbg_ptr_value( *p ); }
c_App::c_App(){
}
c_App* c_App::m_new(){
	DBG_ENTER("App.new")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<152>");
	if((bb_app__app)!=0){
		DBG_BLOCK();
		bbError(String(L"App has already been created",28));
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<153>");
	gc_assign(bb_app__app,this);
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<154>");
	gc_assign(bb_app__delegate,(new c_GameDelegate)->m_new());
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<155>");
	bb_app__game->SetDelegate(bb_app__delegate);
	return this;
}
int c_App::p_OnResize(){
	DBG_ENTER("App.OnResize")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	return 0;
}
int c_App::p_OnCreate(){
	DBG_ENTER("App.OnCreate")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	return 0;
}
int c_App::p_OnSuspend(){
	DBG_ENTER("App.OnSuspend")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	return 0;
}
int c_App::p_OnResume(){
	DBG_ENTER("App.OnResume")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	return 0;
}
int c_App::p_OnUpdate(){
	DBG_ENTER("App.OnUpdate")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	return 0;
}
int c_App::p_OnLoading(){
	DBG_ENTER("App.OnLoading")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	return 0;
}
int c_App::p_OnRender(){
	DBG_ENTER("App.OnRender")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	return 0;
}
int c_App::p_OnClose(){
	DBG_ENTER("App.OnClose")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<177>");
	bb_app_EndApp();
	return 0;
}
int c_App::p_OnBack(){
	DBG_ENTER("App.OnBack")
	c_App *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<181>");
	p_OnClose();
	return 0;
}
void c_App::mark(){
	Object::mark();
}
String c_App::debug(){
	String t="(App)\n";
	return t;
}
c_Game_app::c_Game_app(){
	m_player=0;
	m_raygunbullet_collection=0;
	m_enemy_collection=0;
	m_menu=0;
	m_help=0;
	m_help2=0;
	m_help3=0;
	m_helpoverlay=0;
	m_room=0;
	m_endscreen=0;
	m_leaderboard=0;
	m_helpscreen=0;
	m_tick=0;
	m_tick_health=0;
	m_tick_reload=0;
	m_regenerate=false;
	m_score=0;
	m_points=0;
	m_zombies_killed_in_round=0;
	m_zombies_in_round=0;
	m_zombies_spawned_in_round=0;
	m_round=0;
	m_reload_state=String();
	m_reload_cycle=0;
	m_health=0;
	m_raygunbullet_count=0;
	m_enemy_collision=false;
	m_player_collision=false;
	m_reload_sound=0;
	m_shoot_sound=0;
	m_newRound_sound=0;
	m_punch_sound=0;
	m_knife_sound=0;
	m_click_sound=0;
	m_direction=false;
	m_music_volume=FLOAT(.0);
	m_music_track=String();
	m_health_zombie=0;
	m_gun_damage=0;
	m_PaP_upgrades=0;
	m_weapon_damage=0;
	m_tick_PaP=0;
	m_weapon_upgradeCooldown=false;
	m_PaP_cost=0;
	m_PaP_cycle=0;
	m_spawned_on_screen=0;
	m_name=String();
	m_name_given=String();
	m_pressed_p=false;
	m_settings=0;
	m_path_given=String();
}
c_Game_app* c_Game_app::m_new(){
	DBG_ENTER("Game_app.new")
	c_Game_app *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<20>");
	c_App::m_new();
	return this;
}
String c_Game_app::m_GameState;
int c_Game_app::p_OnCreate(){
	DBG_ENTER("Game_app.OnCreate")
	c_Game_app *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<105>");
	gc_assign(m_player,(new c_Player)->m_new());
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<106>");
	gc_assign(m_raygunbullet_collection,(new c_List)->m_new());
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<107>");
	gc_assign(m_enemy_collection,(new c_List2)->m_new());
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<108>");
	m_enemy_collection->p_AddLast2((new c_Zombie)->m_new());
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<109>");
	m_enemy_collection->p_AddLast2((new c_Zombie)->m_new());
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<111>");
	bb_app_SetUpdateRate(60);
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<115>");
	gc_assign(m_menu,bb_graphics_LoadImage(String(L"menu.png",8),1,c_Image::m_DefaultFlags));
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<116>");
	gc_assign(m_help,bb_graphics_LoadImage(String(L"help.png",8),1,c_Image::m_DefaultFlags));
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<117>");
	gc_assign(m_help2,bb_graphics_LoadImage(String(L"howtoplay.png",13),1,c_Image::m_DefaultFlags));
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<118>");
	gc_assign(m_help3,bb_graphics_LoadImage(String(L"lasthelp.png",12),1,c_Image::m_DefaultFlags));
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<119>");
	gc_assign(m_helpoverlay,bb_graphics_LoadImage(String(L"helpoverlay.png",15),1,c_Image::m_DefaultFlags));
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<121>");
	gc_assign(m_room,bb_graphics_LoadImage(String(L"main room.png",13),1,c_Image::m_DefaultFlags));
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<122>");
	gc_assign(m_endscreen,bb_graphics_LoadImage(String(L"lobby.png",9),1,c_Image::m_DefaultFlags));
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<123>");
	gc_assign(m_leaderboard,bb_graphics_LoadImage(String(L"leaderboard.png",15),1,c_Image::m_DefaultFlags));
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<126>");
	m_helpscreen=0;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<129>");
	m_tick=0;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<130>");
	m_tick_health=99999999;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<131>");
	m_tick_reload=99999999;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<132>");
	m_regenerate=false;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<136>");
	m_GameState=String(L"MENU",4);
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<138>");
	m_score=0;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<139>");
	m_points=0;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<141>");
	m_zombies_killed_in_round=-2;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<142>");
	m_zombies_in_round=1;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<143>");
	m_zombies_spawned_in_round=0;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<145>");
	m_round=1;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<146>");
	m_reload_state=String(L"Yes",3);
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<147>");
	m_reload_cycle=2;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<148>");
	m_health=200;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<149>");
	m_raygunbullet_count=20;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<151>");
	m_enemy_collision=false;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<152>");
	m_player_collision=false;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<154>");
	gc_assign(m_reload_sound,bb_audio_LoadSound(String(L"reload.ogg",10)));
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<155>");
	gc_assign(m_shoot_sound,bb_audio_LoadSound(String(L"shoot.ogg",9)));
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<156>");
	gc_assign(m_newRound_sound,bb_audio_LoadSound(String(L"round change.ogg",16)));
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<157>");
	gc_assign(m_punch_sound,bb_audio_LoadSound(String(L"punch.ogg",9)));
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<158>");
	gc_assign(m_knife_sound,bb_audio_LoadSound(String(L"knife.ogg",9)));
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<159>");
	gc_assign(m_click_sound,bb_audio_LoadSound(String(L"click.ogg",9)));
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<162>");
	m_direction=true;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<164>");
	int t_[]={255,9,35};
	gc_assign(bb_shooter_lbfont,(new c_TFont)->m_new(String(L"tbgold.ttf",10),25,Array<int >(t_,3)));
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<168>");
	m_music_volume=FLOAT(1.0);
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<169>");
	m_music_track=String(L"horde sounds",12);
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<172>");
	m_health_zombie=1;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<174>");
	m_gun_damage=5;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<175>");
	m_PaP_upgrades=0;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<176>");
	m_weapon_damage=m_gun_damage*(m_PaP_upgrades+1);
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<177>");
	m_tick_PaP=99999999;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<178>");
	m_weapon_upgradeCooldown=false;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<179>");
	m_PaP_cost=5000;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<180>");
	m_PaP_cycle=4;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<182>");
	m_spawned_on_screen=2;
	return 0;
}
int c_Game_app::p_OnUpdate(){
	DBG_ENTER("Game_app.OnUpdate")
	c_Game_app *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<195>");
	String t_1=m_GameState;
	DBG_LOCAL(t_1,"1")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<196>");
	if(t_1==String(L"MENU",4)){
		DBG_BLOCK();
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<197>");
		if(m_health<=0){
			DBG_BLOCK();
			m_GameState=String(L"Post Mortem",11);
		}
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<198>");
		if(m_music_track==String(L"horde sounds",12) || m_music_track==String(L"game over",9)){
			DBG_BLOCK();
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<199>");
			m_music_track=String(L"lobby sound",11);
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<200>");
			bb_audio_PlayMusic(String(L"lobby.ogg",9),1);
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<201>");
			bb_audio_ResumeMusic();
		}
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<203>");
		if((bb_input_KeyHit(32))!=0){
			DBG_BLOCK();
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<204>");
			m_GameState=String(L"PLAYING",7);
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<205>");
			bb_audio_PauseMusic();
		}
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<208>");
		if((bb_input_KeyHit(72))!=0){
			DBG_BLOCK();
			m_GameState=String(L"HELP",4);
		}
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<209>");
		if((bb_input_KeyHit(76))!=0){
			DBG_BLOCK();
			m_GameState=String(L"Leaderboard",11);
		}
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<210>");
		if((bb_input_KeyHit(81))!=0){
			DBG_BLOCK();
			bb_app_EndApp();
		}
	}else{
		DBG_BLOCK();
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<211>");
		if(t_1==String(L"HELP",4)){
			DBG_BLOCK();
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<212>");
			if(m_health<=0){
				DBG_BLOCK();
				m_GameState=String(L"Post Mortem",11);
			}
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<213>");
			if((bb_input_KeyHit(27))!=0){
				DBG_BLOCK();
				m_GameState=String(L"MENU",4);
			}
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<214>");
			if((bb_input_KeyHit(65))!=0){
				DBG_BLOCK();
				m_helpscreen-=1;
			}
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<215>");
			if((bb_input_KeyHit(68))!=0){
				DBG_BLOCK();
				m_helpscreen+=1;
			}
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<216>");
			if(m_helpscreen>2){
				DBG_BLOCK();
				m_helpscreen=2;
			}
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<217>");
			if(m_helpscreen<0){
				DBG_BLOCK();
				m_helpscreen=0;
			}
		}else{
			DBG_BLOCK();
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<219>");
			if(t_1==String(L"Leaderboard",11)){
				DBG_BLOCK();
				DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<220>");
				if((bb_input_KeyHit(27))!=0){
					DBG_BLOCK();
					m_GameState=String(L"MENU",4);
				}
				DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<221>");
				bb_shooter_toptenplayers();
			}else{
				DBG_BLOCK();
				DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<223>");
				if(t_1==String(L"Post Mortem",11)){
					DBG_BLOCK();
					DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<224>");
					if(m_score==0){
						DBG_BLOCK();
						m_score+=1;
					}
					DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<227>");
					if(m_music_track==String(L"horde sounds",12) || m_music_track==String(L"lobby sound",11)){
						DBG_BLOCK();
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<228>");
						bb_audio_PauseMusic();
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<229>");
						bb_audio_PlayMusic(String(L"game over.ogg",13),0);
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<230>");
						bb_audio_ResumeMusic();
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<231>");
						bb_audio_SetMusicVolume(m_music_volume);
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<232>");
						m_music_track=String(L"game over",9);
					}
					DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<238>");
					while(m_name.Length()<10){
						DBG_BLOCK();
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<239>");
						int t_char=bb_input_GetChar();
						DBG_LOCAL(t_char,"char")
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<240>");
						if(!((t_char)!=0)){
							DBG_BLOCK();
							break;
						}
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<241>");
						if(t_char>=10){
							DBG_BLOCK();
							DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<242>");
							m_name=m_name+String((Char)(t_char),1);
						}
					}
					DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<247>");
					if((bb_input_KeyHit(8))!=0){
						DBG_BLOCK();
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<248>");
						if(m_name.Length()>0){
							DBG_BLOCK();
							m_name=m_name.Slice(0,m_name.Length()-1);
						}
					}
					DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<252>");
					if((bb_input_KeyHit(27))!=0){
						DBG_BLOCK();
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<253>");
						m_GameState=String(L"MENU",4);
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<254>");
						gc_assign(m_player,(new c_Player)->m_new());
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<255>");
						gc_assign(m_raygunbullet_collection,(new c_List)->m_new());
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<256>");
						gc_assign(m_enemy_collection,(new c_List2)->m_new());
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<257>");
						m_enemy_collection->p_AddLast2((new c_Zombie)->m_new());
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<258>");
						m_enemy_collection->p_AddLast2((new c_Zombie)->m_new());
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<260>");
						m_tick=0;
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<261>");
						m_tick_health=99999999;
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<262>");
						m_tick_reload=99999999;
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<263>");
						m_regenerate=false;
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<265>");
						m_score=0;
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<266>");
						m_points=0;
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<268>");
						m_zombies_killed_in_round=-2;
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<269>");
						m_zombies_in_round=1;
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<270>");
						m_zombies_spawned_in_round=0;
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<272>");
						m_round=1;
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<273>");
						m_reload_state=String(L"Yes",3);
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<274>");
						m_reload_cycle=2;
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<275>");
						m_health=200;
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<276>");
						m_raygunbullet_count=20;
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<278>");
						m_enemy_collision=false;
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<279>");
						m_player_collision=false;
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<281>");
						bb_audio_PlayMusic(String(L"lobby.ogg",9),1);
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<282>");
						bb_audio_SetMusicVolume(m_music_volume);
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<283>");
						bb_audio_ResumeMusic();
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<285>");
						m_health_zombie=1;
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<287>");
						m_gun_damage=5;
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<288>");
						m_PaP_upgrades=0;
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<289>");
						m_tick_PaP=99999999;
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<290>");
						m_weapon_upgradeCooldown=false;
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<291>");
						m_weapon_damage=m_gun_damage*(m_PaP_upgrades+1);
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<292>");
						m_PaP_cost=5000;
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<293>");
						m_PaP_cycle=0;
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<295>");
						m_spawned_on_screen=2;
					}
					DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<299>");
					if((bb_input_KeyHit(13))!=0){
						DBG_BLOCK();
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<300>");
						bb_shooter_writenamesandscore(m_name,m_score);
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<301>");
						m_GameState=String(L"MENU",4);
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<302>");
						gc_assign(m_player,(new c_Player)->m_new());
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<303>");
						gc_assign(m_raygunbullet_collection,(new c_List)->m_new());
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<304>");
						gc_assign(m_enemy_collection,(new c_List2)->m_new());
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<305>");
						m_enemy_collection->p_AddLast2((new c_Zombie)->m_new());
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<306>");
						m_enemy_collection->p_AddLast2((new c_Zombie)->m_new());
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<308>");
						m_tick=0;
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<309>");
						m_tick_health=99999999;
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<310>");
						m_tick_reload=99999999;
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<311>");
						m_regenerate=false;
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<313>");
						m_score=0;
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<314>");
						m_points=0;
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<316>");
						m_zombies_killed_in_round=-2;
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<317>");
						m_zombies_in_round=1;
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<318>");
						m_zombies_spawned_in_round=0;
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<320>");
						m_round=1;
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<321>");
						m_reload_state=String(L"Yes",3);
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<322>");
						m_reload_cycle=2;
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<323>");
						m_health=200;
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<324>");
						m_raygunbullet_count=20;
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<326>");
						m_enemy_collision=false;
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<327>");
						m_player_collision=false;
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<329>");
						bb_audio_PlayMusic(String(L"lobby.ogg",9),1);
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<330>");
						bb_audio_SetMusicVolume(m_music_volume);
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<331>");
						bb_audio_ResumeMusic();
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<333>");
						m_health_zombie=1;
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<335>");
						m_gun_damage=5;
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<336>");
						m_PaP_upgrades=0;
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<337>");
						m_tick_PaP=99999999;
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<338>");
						m_weapon_upgradeCooldown=false;
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<339>");
						m_weapon_damage=m_gun_damage*(m_PaP_upgrades+1);
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<340>");
						m_PaP_cost=5000;
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<341>");
						m_PaP_cycle=0;
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<343>");
						m_spawned_on_screen=2;
					}
				}else{
					DBG_BLOCK();
					DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<349>");
					if(t_1==String(L"NewRound",8)){
						DBG_BLOCK();
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<350>");
						if(m_health<=0){
							DBG_BLOCK();
							m_GameState=String(L"Post Mortem",11);
						}
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<351>");
						bb_audio_StopMusic();
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<352>");
						m_music_track=String(L"new round",9);
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<353>");
						bb_audio_PlaySound(m_newRound_sound,5,0);
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<354>");
						m_round+=1;
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<355>");
						m_zombies_in_round=int(Float(m_zombies_in_round)+bb_random_Rnd2(FLOAT(4.0),FLOAT(10.0)));
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<356>");
						if(m_zombies_in_round<0){
							DBG_BLOCK();
							m_zombies_in_round=0;
						}
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<358>");
						m_zombies_spawned_in_round=0;
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<359>");
						m_zombies_killed_in_round=-2;
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<360>");
						m_spawned_on_screen=2;
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<362>");
						if(m_round % 2==0){
							DBG_BLOCK();
							m_health_zombie=m_health_zombie*4;
						}
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<365>");
						m_enemy_collection->p_AddLast2((new c_Zombie)->m_new());
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<366>");
						m_enemy_collection->p_AddLast2((new c_Zombie)->m_new());
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<367>");
						m_GameState=String(L"PLAYING",7);
					}else{
						DBG_BLOCK();
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<373>");
						if(t_1==String(L"PLAYING",7)){
							DBG_BLOCK();
							DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<374>");
							if(m_health<=0){
								DBG_BLOCK();
								m_GameState=String(L"Post Mortem",11);
							}
							DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<376>");
							if(m_tick<121){
								DBG_BLOCK();
								m_tick+=1;
							}
							DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<377>");
							if(m_tick==121){
								DBG_BLOCK();
								m_tick=0;
							}
							DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<379>");
							if(m_tick==m_tick_PaP && m_PaP_cycle<3){
								DBG_BLOCK();
								m_PaP_cycle+=1;
							}
							DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<381>");
							if(m_weapon_upgradeCooldown==true && m_PaP_cycle>=3){
								DBG_BLOCK();
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<382>");
								m_weapon_upgradeCooldown=false;
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<383>");
								m_tick_PaP=99999999;
							}
							DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<387>");
							if(m_points>=m_PaP_cost && ((bb_input_KeyHit(80))!=0) && m_weapon_upgradeCooldown==false){
								DBG_BLOCK();
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<388>");
								m_points-=m_PaP_cost;
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<389>");
								m_PaP_upgrades+=1;
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<390>");
								m_weapon_damage=15*(m_PaP_upgrades+1);
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<391>");
								m_tick_PaP=m_tick;
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<392>");
								m_weapon_upgradeCooldown=true;
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<393>");
								m_PaP_cost+=1000;
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<394>");
								bb_audio_PlaySound(m_punch_sound,6,0);
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<395>");
								m_PaP_cycle=0;
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<398>");
								m_raygunbullet_count=0;
							}
							DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<401>");
							if(m_music_track==String(L"lobby sound",11) || m_music_track==String(L"new round",9)){
								DBG_BLOCK();
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<402>");
								bb_audio_PlayMusic(String(L"horde sounds.ogg",16),1);
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<403>");
								m_music_track=String(L"horde sounds",12);
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<404>");
								bb_audio_SetMusicVolume(m_music_volume);
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<405>");
								bb_audio_ResumeMusic();
							}
							DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<408>");
							if((bb_input_KeyHit(27))!=0){
								DBG_BLOCK();
								m_GameState=String(L"MENU",4);
							}
							DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<410>");
							if(m_raygunbullet_count<20){
								DBG_BLOCK();
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<411>");
								if(((bb_input_KeyHit(32))!=0) && m_weapon_upgradeCooldown==false && m_reload_cycle>=1){
									DBG_BLOCK();
									DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<412>");
									bb_audio_PlaySound(m_shoot_sound,0,0);
									DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<413>");
									m_raygunbullet_collection->p_AddLast((new c_RayGunBullet)->m_new(m_player->m_x,m_player->m_y));
									DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<414>");
									m_raygunbullet_count+=1;
								}
							}
							DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<419>");
							if(m_raygunbullet_count==20 && ((bb_input_KeyHit(32))!=0)){
								DBG_BLOCK();
								bb_audio_PlaySound(m_click_sound,1,0);
							}
							DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<422>");
							if(m_raygunbullet_count==0){
								DBG_BLOCK();
								m_reload_state=String(L"No",2);
							}
							DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<423>");
							if(m_raygunbullet_count==20){
								DBG_BLOCK();
								m_reload_state=String(L"Yes",3);
							}
							DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<426>");
							if(m_tick==m_tick_reload && m_reload_cycle<2){
								DBG_BLOCK();
								m_reload_cycle+=1;
							}
							DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<431>");
							if(((bb_input_KeyDown(82))!=0) && m_reload_cycle>=2 && m_raygunbullet_count==0==false){
								DBG_BLOCK();
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<432>");
								m_raygunbullet_count=0;
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<433>");
								m_reload_cycle=0;
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<434>");
								m_tick_reload=m_tick;
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<435>");
								bb_audio_PlaySound(m_reload_sound,2,0);
							}
							DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<441>");
							if(m_tick==m_tick_health){
								DBG_BLOCK();
								m_regenerate=true;
							}
							DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<442>");
							if(m_regenerate==true && m_health<200){
								DBG_BLOCK();
								m_health+=1;
							}
							DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<443>");
							if(m_health==200){
								DBG_BLOCK();
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<444>");
								m_regenerate=false;
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<445>");
								m_tick_health=99999999;
							}
							DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<454>");
							c_Enumerator3* t_=m_raygunbullet_collection->p_ObjectEnumerator();
							while(t_->p_HasNext()){
								DBG_BLOCK();
								c_RayGunBullet* t_raygunbullet=t_->p_NextObject();
								DBG_LOCAL(t_raygunbullet,"raygunbullet")
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<455>");
								t_raygunbullet->p_Move2(m_GameState);
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<456>");
								if(t_raygunbullet->m_y<FLOAT(0.0)){
									DBG_BLOCK();
									m_raygunbullet_collection->p_Remove(t_raygunbullet);
								}
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<457>");
								if(t_raygunbullet->m_y>FLOAT(459.0)){
									DBG_BLOCK();
									m_raygunbullet_collection->p_Remove(t_raygunbullet);
								}
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<458>");
								if(t_raygunbullet->m_x<FLOAT(0.0)){
									DBG_BLOCK();
									m_raygunbullet_collection->p_Remove(t_raygunbullet);
								}
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<459>");
								if(t_raygunbullet->m_x>FLOAT(630.0)){
									DBG_BLOCK();
									m_raygunbullet_collection->p_Remove(t_raygunbullet);
								}
							}
							DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<462>");
							m_player->p_Move(m_GameState,m_tick);
							DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<463>");
							if(bb_input_KeyDown(68)==1 && bb_input_KeyDown(65)==0 || bb_input_KeyDown(68)==0 && bb_input_KeyDown(65)==1){
								DBG_BLOCK();
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<464>");
								if((bb_input_KeyDown(68))!=0){
									DBG_BLOCK();
									m_direction=true;
								}
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<465>");
								if((bb_input_KeyDown(65))!=0){
									DBG_BLOCK();
									m_direction=false;
								}
							}
							DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<478>");
							c_Enumerator4* t_2=m_enemy_collection->p_ObjectEnumerator();
							while(t_2->p_HasNext()){
								DBG_BLOCK();
								c_Zombie* t_enemy=t_2->p_NextObject();
								DBG_LOCAL(t_enemy,"enemy")
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<481>");
								m_enemy_collision=false;
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<482>");
								m_player_collision=false;
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<483>");
								if(bb_shooter_intersects(int(t_enemy->m_x),int(t_enemy->m_y),22,27,int(t_enemy->m_x),int(t_enemy->m_y),22,27)){
									DBG_BLOCK();
									m_enemy_collision=true;
								}
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<484>");
								if(bb_shooter_intersects(int(m_player->m_x),int(m_player->m_y),20,22,int(t_enemy->m_x),int(t_enemy->m_y),22,27)){
									DBG_BLOCK();
									m_player_collision=true;
								}
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<485>");
								t_enemy->p_Move3(Float(m_player->p_give_x()),Float(m_player->p_give_y()),m_GameState,m_enemy_collision,m_player_collision,m_tick);
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<490>");
								if(bb_shooter_intersects(int(m_player->m_x),int(m_player->m_y),20,22,int(t_enemy->m_x),int(t_enemy->m_y),22,27)){
									DBG_BLOCK();
									DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<492>");
									m_health-=1;
									DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<493>");
									if(m_health<=0){
										DBG_BLOCK();
										m_GameState=String(L"Post Mortem",11);
									}
									DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<494>");
									m_tick_health=m_tick;
									DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<495>");
									m_regenerate=false;
									DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<497>");
									if((bb_input_KeyHit(86))!=0){
										DBG_BLOCK();
										DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<498>");
										bb_audio_PlaySound(m_knife_sound,4,0);
										DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<499>");
										m_points+=10;
										DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<500>");
										m_score+=10;
										DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<502>");
										if(t_enemy->p_dead(m_health_zombie,10)==1){
											DBG_BLOCK();
											DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<503>");
											m_enemy_collection->p_Remove3(t_enemy);
											DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<504>");
											m_points+=130;
											DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<505>");
											m_score+=130;
											DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<506>");
											m_zombies_killed_in_round+=1;
											DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<507>");
											m_spawned_on_screen-=1;
										}
									}
								}
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<515>");
								c_Enumerator3* t_3=m_raygunbullet_collection->p_ObjectEnumerator();
								while(t_3->p_HasNext()){
									DBG_BLOCK();
									c_RayGunBullet* t_raygunbullet2=t_3->p_NextObject();
									DBG_LOCAL(t_raygunbullet2,"raygunbullet")
									DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<519>");
									if(bb_shooter_intersects(int(t_raygunbullet2->m_x),int(t_raygunbullet2->m_y),10,21,int(t_enemy->m_x),int(t_enemy->m_y),22,27)){
										DBG_BLOCK();
										DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<520>");
										m_points+=10;
										DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<521>");
										m_score+=10;
										DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<522>");
										if(t_raygunbullet2->p_health(m_PaP_upgrades)==1){
											DBG_BLOCK();
											m_raygunbullet_collection->p_Remove(t_raygunbullet2);
										}
										DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<523>");
										if(t_enemy->p_dead(m_health_zombie,m_weapon_damage)==1){
											DBG_BLOCK();
											DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<524>");
											m_enemy_collection->p_Remove3(t_enemy);
											DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<525>");
											m_points+=50;
											DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<526>");
											m_score+=50;
											DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<527>");
											m_zombies_killed_in_round+=1;
											DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<528>");
											m_spawned_on_screen-=1;
										}
									}
								}
							}
							DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<536>");
							if(m_zombies_spawned_in_round<m_zombies_in_round && m_spawned_on_screen<10){
								DBG_BLOCK();
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<540>");
								m_enemy_collection->p_AddLast2((new c_Zombie)->m_new());
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<541>");
								m_zombies_spawned_in_round+=1;
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<542>");
								m_spawned_on_screen+=1;
							}
							DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<553>");
							if(m_zombies_spawned_in_round==m_zombies_in_round && m_zombies_killed_in_round==m_zombies_in_round){
								DBG_BLOCK();
								m_GameState=String(L"NewRound",8);
							}
							DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<559>");
							if(m_health<=0){
								DBG_BLOCK();
								m_GameState=String(L"Post Mortem",11);
							}
						}
					}
				}
			}
		}
	}
	return 0;
}
int c_Game_app::p_OnRender(){
	DBG_ENTER("Game_app.OnRender")
	c_Game_app *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<571>");
	String t_2=m_GameState;
	DBG_LOCAL(t_2,"2")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<572>");
	if(t_2==String(L"MENU",4)){
		DBG_BLOCK();
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<573>");
		bb_graphics_DrawImage(m_menu,FLOAT(0.0),FLOAT(0.0),0);
	}else{
		DBG_BLOCK();
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<574>");
		if(t_2==String(L"HELP",4)){
			DBG_BLOCK();
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<575>");
			if(m_helpscreen==0){
				DBG_BLOCK();
				DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<576>");
				bb_graphics_DrawImage(m_help,FLOAT(0.0),FLOAT(0.0),0);
				DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<577>");
				bb_graphics_DrawImage(m_helpoverlay,FLOAT(0.0),FLOAT(0.0),0);
			}
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<579>");
			if(m_helpscreen==1){
				DBG_BLOCK();
				DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<580>");
				bb_graphics_DrawImage(m_help2,FLOAT(0.0),FLOAT(0.0),0);
				DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<581>");
				bb_graphics_DrawImage(m_helpoverlay,FLOAT(0.0),FLOAT(0.0),0);
			}
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<583>");
			if(m_helpscreen==2){
				DBG_BLOCK();
				DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<584>");
				bb_graphics_DrawImage(m_help3,FLOAT(0.0),FLOAT(0.0),0);
				DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<585>");
				bb_graphics_DrawImage(m_helpoverlay,FLOAT(0.0),FLOAT(0.0),0);
			}
		}else{
			DBG_BLOCK();
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<588>");
			if(t_2==String(L"NewRound",8)){
				DBG_BLOCK();
				DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<589>");
				bb_graphics_Cls(FLOAT(255.0),FLOAT(255.0),FLOAT(255.0));
				DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<590>");
				bb_graphics_SetColor(FLOAT(0.0),FLOAT(0.0),FLOAT(0.0));
				DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<591>");
				bb_graphics_DrawImage(m_player->m_sprite,m_player->m_x,m_player->m_y,0);
			}else{
				DBG_BLOCK();
				DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<593>");
				if(t_2==String(L"Leaderboard",11)){
					DBG_BLOCK();
					DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<594>");
					bb_graphics_Cls(FLOAT(0.0),FLOAT(0.0),FLOAT(0.0));
					DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<595>");
					bb_graphics_DrawImage(m_leaderboard,FLOAT(0.0),FLOAT(0.0),0);
					DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<597>");
					for(int t_rank=0;t_rank<10;t_rank=t_rank+1){
						DBG_BLOCK();
						DBG_LOCAL(t_rank,"rank")
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<598>");
						bb_shooter_lbfont->p_DrawText(String(L" ",1)+String(t_rank+1)+String(L")    ",5)+String(bb_shooter_playerscores.At(t_rank))+String(L" ",1)+bb_shooter_names.At(t_rank),0,0+t_rank*50,0,0,0,0);
					}
				}else{
					DBG_BLOCK();
					DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<602>");
					if(t_2==String(L"Post Mortem",11)){
						DBG_BLOCK();
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<603>");
						bb_graphics_Cls(FLOAT(125.0),FLOAT(125.0),FLOAT(125.0));
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<604>");
						bb_graphics_SetColor(FLOAT(115.0),FLOAT(9.0),FLOAT(35.0));
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<605>");
						bb_graphics_DrawImage(m_endscreen,FLOAT(0.0),FLOAT(0.0),0);
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<606>");
						bb_graphics_SetColor(FLOAT(115.0),FLOAT(9.0),FLOAT(35.0));
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<607>");
						bb_graphics_DrawRect(FLOAT(1000.0),FLOAT(1000.0),FLOAT(1000.0),FLOAT(1000.0));
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<608>");
						bb_graphics_SetColor(FLOAT(115.0),FLOAT(9.0),FLOAT(35.0));
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<609>");
						bb_graphics_DrawText(String(L"FINAL SCORE:",12)+String(m_score),FLOAT(290.0),FLOAT(235.0),FLOAT(0.0),FLOAT(0.0));
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<610>");
						bb_graphics_DrawImage(m_player->m_sprite,m_player->m_x,m_player->m_y,0);
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<615>");
						bb_graphics_DrawText(String(L"In Memoriam (p.s way to go, loser):",35)+m_name_given+m_name,FLOAT(50.0),FLOAT(75.0),FLOAT(0.0),FLOAT(0.0));
					}else{
						DBG_BLOCK();
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<617>");
						if(t_2==String(L"settings",8)){
							DBG_BLOCK();
							DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<618>");
							m_pressed_p=false;
							DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<619>");
							if(m_pressed_p==false){
								DBG_BLOCK();
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<620>");
								bb_graphics_Cls(FLOAT(125.0),FLOAT(125.0),FLOAT(125.0));
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<621>");
								bb_graphics_SetColor(FLOAT(0.0),FLOAT(0.0),FLOAT(0.0));
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<622>");
								bb_graphics_DrawImage(m_settings,FLOAT(0.0),FLOAT(0.0),0);
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<623>");
								bb_graphics_DrawRect(FLOAT(1000.0),FLOAT(1000.0),FLOAT(1000.0),FLOAT(1000.0));
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<624>");
								bb_graphics_DrawText(String(L"Edit Path (P)",13),FLOAT(0.0),FLOAT(235.0),FLOAT(0.0),FLOAT(0.0));
							}
							DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<626>");
							if(m_pressed_p==true){
								DBG_BLOCK();
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<627>");
								bb_graphics_DrawText(String(L"Enter path:",11)+m_path_given+bb_shooter_path,FLOAT(50.0),FLOAT(75.0),FLOAT(0.0),FLOAT(0.0));
							}
						}else{
							DBG_BLOCK();
							DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<630>");
							if(t_2==String(L"PLAYING",7)){
								DBG_BLOCK();
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<631>");
								bb_graphics_Cls(FLOAT(255.0),FLOAT(50.0),FLOAT(12.0));
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<632>");
								bb_graphics_DrawImage(m_room,FLOAT(0.0),FLOAT(0.0),0);
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<633>");
								bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(255.0));
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<634>");
								bb_graphics_DrawRect(FLOAT(0.0),FLOAT(0.0),FLOAT(1000.0),FLOAT(0.0));
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<635>");
								bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(225.0));
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<636>");
								bb_graphics_DrawText(String(L"Points:",7)+String(m_points),FLOAT(0.0),FLOAT(300.0),FLOAT(0.0),FLOAT(0.0));
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<638>");
								bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(255.0));
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<639>");
								bb_graphics_DrawRect(FLOAT(100.0),FLOAT(0.0),FLOAT(115.0),FLOAT(0.0));
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<640>");
								bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(225.0));
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<641>");
								bb_graphics_DrawText(String(L"Ammo:",5)+String(20-m_raygunbullet_count),FLOAT(560.0),FLOAT(300.0),FLOAT(0.0),FLOAT(0.0));
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<643>");
								bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(255.0));
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<644>");
								bb_graphics_DrawRect(FLOAT(80.0),FLOAT(0.0),FLOAT(215.0),FLOAT(0.0));
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<645>");
								bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(225.0));
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<646>");
								bb_graphics_DrawText(String(L"Reload?:",8)+m_reload_state,FLOAT(560.0),FLOAT(314.0),FLOAT(0.0),FLOAT(0.0));
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<648>");
								bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(255.0));
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<649>");
								bb_graphics_DrawRect(FLOAT(80.0),FLOAT(0.0),FLOAT(215.0),FLOAT(0.0));
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<650>");
								bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(225.0));
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<651>");
								bb_graphics_DrawText(String(L"Health:",7)+String(m_health),FLOAT(0.0),FLOAT(314.0),FLOAT(0.0),FLOAT(0.0));
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<653>");
								bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(255.0));
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<654>");
								bb_graphics_DrawRect(FLOAT(80.0),FLOAT(0.0),FLOAT(215.0),FLOAT(0.0));
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<655>");
								bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(225.0));
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<656>");
								bb_graphics_DrawText(String(L"Sprint:",7)+String(m_player->p_give_sprint_energy()),FLOAT(0.0),FLOAT(328.0),FLOAT(0.0),FLOAT(0.0));
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<658>");
								bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(255.0));
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<659>");
								bb_graphics_DrawRect(FLOAT(80.0),FLOAT(0.0),FLOAT(215.0),FLOAT(0.0));
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<660>");
								bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(225.0));
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<661>");
								bb_graphics_DrawText(String(L"Round:",6)+String(m_round),FLOAT(100.0),FLOAT(0.0),FLOAT(0.0),FLOAT(0.0));
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<663>");
								bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(255.0));
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<664>");
								bb_graphics_DrawRect(FLOAT(80.0),FLOAT(0.0),FLOAT(215.0),FLOAT(0.0));
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<665>");
								bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(225.0));
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<666>");
								bb_graphics_DrawText(String(L"Pack-A-Punch Upgrades:",22)+String(m_PaP_upgrades),FLOAT(200.0),FLOAT(0.0),FLOAT(0.0),FLOAT(0.0));
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<668>");
								bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(255.0));
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<669>");
								bb_graphics_DrawRect(FLOAT(80.0),FLOAT(0.0),FLOAT(215.0),FLOAT(0.0));
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<670>");
								bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(225.0));
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<671>");
								bb_graphics_DrawText(String(L"Pack-A-Punch Cost:",18)+String(m_PaP_cost),FLOAT(400.0),FLOAT(0.0),FLOAT(0.0),FLOAT(0.0));
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<674>");
								if(m_direction==true){
									DBG_BLOCK();
									bb_graphics_DrawImage(m_player->m_sprite,m_player->m_x,m_player->m_y,0);
								}
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<675>");
								if(m_direction==false){
									DBG_BLOCK();
									bb_graphics_DrawImage(m_player->m_sprite2,m_player->m_x,m_player->m_y,0);
								}
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<676>");
								c_Enumerator4* t_=m_enemy_collection->p_ObjectEnumerator();
								while(t_->p_HasNext()){
									DBG_BLOCK();
									c_Zombie* t_enemy=t_->p_NextObject();
									DBG_LOCAL(t_enemy,"enemy")
									DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<677>");
									bb_graphics_DrawImage(t_enemy->m_sprite,t_enemy->m_x,t_enemy->m_y,0);
									DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<678>");
									c_Enumerator3* t_3=m_raygunbullet_collection->p_ObjectEnumerator();
									while(t_3->p_HasNext()){
										DBG_BLOCK();
										c_RayGunBullet* t_raygunbullet=t_3->p_NextObject();
										DBG_LOCAL(t_raygunbullet,"raygunbullet")
										DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<679>");
										bb_graphics_DrawImage(t_raygunbullet->m_sprite,t_raygunbullet->m_x,t_raygunbullet->m_y,0);
									}
								}
							}
						}
					}
				}
			}
		}
	}
	return 0;
}
void c_Game_app::mark(){
	c_App::mark();
	gc_mark_q(m_player);
	gc_mark_q(m_raygunbullet_collection);
	gc_mark_q(m_enemy_collection);
	gc_mark_q(m_menu);
	gc_mark_q(m_help);
	gc_mark_q(m_help2);
	gc_mark_q(m_help3);
	gc_mark_q(m_helpoverlay);
	gc_mark_q(m_room);
	gc_mark_q(m_endscreen);
	gc_mark_q(m_leaderboard);
	gc_mark_q(m_reload_sound);
	gc_mark_q(m_shoot_sound);
	gc_mark_q(m_newRound_sound);
	gc_mark_q(m_punch_sound);
	gc_mark_q(m_knife_sound);
	gc_mark_q(m_click_sound);
	gc_mark_q(m_settings);
}
String c_Game_app::debug(){
	String t="(Game_app)\n";
	t=c_App::debug()+t;
	t+=dbg_decl("player",&m_player);
	t+=dbg_decl("raygunbullet_collection",&m_raygunbullet_collection);
	t+=dbg_decl("enemy_collection",&m_enemy_collection);
	t+=dbg_decl("menu",&m_menu);
	t+=dbg_decl("help",&m_help);
	t+=dbg_decl("help2",&m_help2);
	t+=dbg_decl("help3",&m_help3);
	t+=dbg_decl("helpoverlay",&m_helpoverlay);
	t+=dbg_decl("room",&m_room);
	t+=dbg_decl("endscreen",&m_endscreen);
	t+=dbg_decl("leaderboard",&m_leaderboard);
	t+=dbg_decl("settings",&m_settings);
	t+=dbg_decl("pressed_p",&m_pressed_p);
	t+=dbg_decl("path_given",&m_path_given);
	t+=dbg_decl("helpscreen",&m_helpscreen);
	t+=dbg_decl("score",&m_score);
	t+=dbg_decl("points",&m_points);
	t+=dbg_decl("raygunbullet_count",&m_raygunbullet_count);
	t+=dbg_decl("GameState",&c_Game_app::m_GameState);
	t+=dbg_decl("round",&m_round);
	t+=dbg_decl("zombies_killed_in_round",&m_zombies_killed_in_round);
	t+=dbg_decl("zombies_in_round",&m_zombies_in_round);
	t+=dbg_decl("zombies_spawned_in_round",&m_zombies_spawned_in_round);
	t+=dbg_decl("reload_state",&m_reload_state);
	t+=dbg_decl("reload_cycle",&m_reload_cycle);
	t+=dbg_decl("health",&m_health);
	t+=dbg_decl("enemy_collision",&m_enemy_collision);
	t+=dbg_decl("player_collision",&m_player_collision);
	t+=dbg_decl("tick",&m_tick);
	t+=dbg_decl("tick_health",&m_tick_health);
	t+=dbg_decl("tick_reload",&m_tick_reload);
	t+=dbg_decl("regenerate",&m_regenerate);
	t+=dbg_decl("newRound_sound",&m_newRound_sound);
	t+=dbg_decl("reload_sound",&m_reload_sound);
	t+=dbg_decl("shoot_sound",&m_shoot_sound);
	t+=dbg_decl("punch_sound",&m_punch_sound);
	t+=dbg_decl("knife_sound",&m_knife_sound);
	t+=dbg_decl("click_sound",&m_click_sound);
	t+=dbg_decl("direction",&m_direction);
	t+=dbg_decl("music_volume",&m_music_volume);
	t+=dbg_decl("music_track",&m_music_track);
	t+=dbg_decl("health_zombie",&m_health_zombie);
	t+=dbg_decl("name",&m_name);
	t+=dbg_decl("name_given",&m_name_given);
	t+=dbg_decl("gun_damage",&m_gun_damage);
	t+=dbg_decl("PaP_upgrades",&m_PaP_upgrades);
	t+=dbg_decl("weapon_damage",&m_weapon_damage);
	t+=dbg_decl("tick_PaP",&m_tick_PaP);
	t+=dbg_decl("weapon_upgradeCooldown",&m_weapon_upgradeCooldown);
	t+=dbg_decl("PaP_cost",&m_PaP_cost);
	t+=dbg_decl("PaP_cycle",&m_PaP_cycle);
	t+=dbg_decl("spawned_on_screen",&m_spawned_on_screen);
	return t;
}
c_App* bb_app__app;
c_GameDelegate::c_GameDelegate(){
	m__graphics=0;
	m__audio=0;
	m__input=0;
}
c_GameDelegate* c_GameDelegate::m_new(){
	DBG_ENTER("GameDelegate.new")
	c_GameDelegate *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<65>");
	return this;
}
void c_GameDelegate::StartGame(){
	DBG_ENTER("GameDelegate.StartGame")
	c_GameDelegate *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<75>");
	gc_assign(m__graphics,(new gxtkGraphics));
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<76>");
	bb_graphics_SetGraphicsDevice(m__graphics);
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<77>");
	bb_graphics_SetFont(0,32);
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<79>");
	gc_assign(m__audio,(new gxtkAudio));
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<80>");
	bb_audio_SetAudioDevice(m__audio);
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<82>");
	gc_assign(m__input,(new c_InputDevice)->m_new());
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<83>");
	bb_input_SetInputDevice(m__input);
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<85>");
	bb_app_ValidateDeviceWindow(false);
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<87>");
	bb_app_EnumDisplayModes();
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<89>");
	bb_app__app->p_OnCreate();
}
void c_GameDelegate::SuspendGame(){
	DBG_ENTER("GameDelegate.SuspendGame")
	c_GameDelegate *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<93>");
	bb_app__app->p_OnSuspend();
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<94>");
	m__audio->Suspend();
}
void c_GameDelegate::ResumeGame(){
	DBG_ENTER("GameDelegate.ResumeGame")
	c_GameDelegate *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<98>");
	m__audio->Resume();
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<99>");
	bb_app__app->p_OnResume();
}
void c_GameDelegate::UpdateGame(){
	DBG_ENTER("GameDelegate.UpdateGame")
	c_GameDelegate *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<103>");
	bb_app_ValidateDeviceWindow(true);
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<104>");
	m__input->p_BeginUpdate();
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<105>");
	bb_app__app->p_OnUpdate();
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<106>");
	m__input->p_EndUpdate();
}
void c_GameDelegate::RenderGame(){
	DBG_ENTER("GameDelegate.RenderGame")
	c_GameDelegate *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<110>");
	bb_app_ValidateDeviceWindow(true);
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<111>");
	int t_mode=m__graphics->BeginRender();
	DBG_LOCAL(t_mode,"mode")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<112>");
	if((t_mode)!=0){
		DBG_BLOCK();
		bb_graphics_BeginRender();
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<113>");
	if(t_mode==2){
		DBG_BLOCK();
		bb_app__app->p_OnLoading();
	}else{
		DBG_BLOCK();
		bb_app__app->p_OnRender();
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<114>");
	if((t_mode)!=0){
		DBG_BLOCK();
		bb_graphics_EndRender();
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<115>");
	m__graphics->EndRender();
}
void c_GameDelegate::KeyEvent(int t_event,int t_data){
	DBG_ENTER("GameDelegate.KeyEvent")
	c_GameDelegate *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_event,"event")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<119>");
	m__input->p_KeyEvent(t_event,t_data);
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<120>");
	if(t_event!=1){
		DBG_BLOCK();
		return;
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<121>");
	int t_1=t_data;
	DBG_LOCAL(t_1,"1")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<122>");
	if(t_1==432){
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<123>");
		bb_app__app->p_OnClose();
	}else{
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<124>");
		if(t_1==416){
			DBG_BLOCK();
			DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<125>");
			bb_app__app->p_OnBack();
		}
	}
}
void c_GameDelegate::MouseEvent(int t_event,int t_data,Float t_x,Float t_y){
	DBG_ENTER("GameDelegate.MouseEvent")
	c_GameDelegate *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_event,"event")
	DBG_LOCAL(t_data,"data")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<130>");
	m__input->p_MouseEvent(t_event,t_data,t_x,t_y);
}
void c_GameDelegate::TouchEvent(int t_event,int t_data,Float t_x,Float t_y){
	DBG_ENTER("GameDelegate.TouchEvent")
	c_GameDelegate *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_event,"event")
	DBG_LOCAL(t_data,"data")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<134>");
	m__input->p_TouchEvent(t_event,t_data,t_x,t_y);
}
void c_GameDelegate::MotionEvent(int t_event,int t_data,Float t_x,Float t_y,Float t_z){
	DBG_ENTER("GameDelegate.MotionEvent")
	c_GameDelegate *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_event,"event")
	DBG_LOCAL(t_data,"data")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_z,"z")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<138>");
	m__input->p_MotionEvent(t_event,t_data,t_x,t_y,t_z);
}
void c_GameDelegate::DiscardGraphics(){
	DBG_ENTER("GameDelegate.DiscardGraphics")
	c_GameDelegate *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<142>");
	m__graphics->DiscardGraphics();
}
void c_GameDelegate::mark(){
	BBGameDelegate::mark();
	gc_mark_q(m__graphics);
	gc_mark_q(m__audio);
	gc_mark_q(m__input);
}
String c_GameDelegate::debug(){
	String t="(GameDelegate)\n";
	t+=dbg_decl("_graphics",&m__graphics);
	t+=dbg_decl("_audio",&m__audio);
	t+=dbg_decl("_input",&m__input);
	return t;
}
c_GameDelegate* bb_app__delegate;
BBGame* bb_app__game;
c_Game_app* bb_shooter_Game;
int bbMain(){
	DBG_ENTER("Main")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<16>");
	gc_assign(bb_shooter_Game,(new c_Game_app)->m_new());
	return 0;
}
gxtkGraphics* bb_graphics_device;
int bb_graphics_SetGraphicsDevice(gxtkGraphics* t_dev){
	DBG_ENTER("SetGraphicsDevice")
	DBG_LOCAL(t_dev,"dev")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<63>");
	gc_assign(bb_graphics_device,t_dev);
	return 0;
}
c_Image::c_Image(){
	m_surface=0;
	m_width=0;
	m_height=0;
	m_frames=Array<c_Frame* >();
	m_flags=0;
	m_tx=FLOAT(.0);
	m_ty=FLOAT(.0);
	m_source=0;
}
int c_Image::m_DefaultFlags;
c_Image* c_Image::m_new(){
	DBG_ENTER("Image.new")
	c_Image *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<70>");
	return this;
}
int c_Image::p_SetHandle(Float t_tx,Float t_ty){
	DBG_ENTER("Image.SetHandle")
	c_Image *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_tx,"tx")
	DBG_LOCAL(t_ty,"ty")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<114>");
	this->m_tx=t_tx;
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<115>");
	this->m_ty=t_ty;
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<116>");
	this->m_flags=this->m_flags&-2;
	return 0;
}
int c_Image::p_ApplyFlags(int t_iflags){
	DBG_ENTER("Image.ApplyFlags")
	c_Image *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_iflags,"iflags")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<197>");
	m_flags=t_iflags;
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<199>");
	if((m_flags&2)!=0){
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<200>");
		Array<c_Frame* > t_=m_frames;
		int t_2=0;
		while(t_2<t_.Length()){
			DBG_BLOCK();
			c_Frame* t_f=t_.At(t_2);
			t_2=t_2+1;
			DBG_LOCAL(t_f,"f")
			DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<201>");
			t_f->m_x+=1;
		}
		DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<203>");
		m_width-=2;
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<206>");
	if((m_flags&4)!=0){
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<207>");
		Array<c_Frame* > t_3=m_frames;
		int t_4=0;
		while(t_4<t_3.Length()){
			DBG_BLOCK();
			c_Frame* t_f2=t_3.At(t_4);
			t_4=t_4+1;
			DBG_LOCAL(t_f2,"f")
			DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<208>");
			t_f2->m_y+=1;
		}
		DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<210>");
		m_height-=2;
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<213>");
	if((m_flags&1)!=0){
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<214>");
		p_SetHandle(Float(m_width)/FLOAT(2.0),Float(m_height)/FLOAT(2.0));
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<217>");
	if(m_frames.Length()==1 && m_frames.At(0)->m_x==0 && m_frames.At(0)->m_y==0 && m_width==m_surface->Width() && m_height==m_surface->Height()){
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<218>");
		m_flags|=65536;
	}
	return 0;
}
c_Image* c_Image::p_Init(gxtkSurface* t_surf,int t_nframes,int t_iflags){
	DBG_ENTER("Image.Init")
	c_Image *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_surf,"surf")
	DBG_LOCAL(t_nframes,"nframes")
	DBG_LOCAL(t_iflags,"iflags")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<143>");
	if((m_surface)!=0){
		DBG_BLOCK();
		bbError(String(L"Image already initialized",25));
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<144>");
	gc_assign(m_surface,t_surf);
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<146>");
	m_width=m_surface->Width()/t_nframes;
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<147>");
	m_height=m_surface->Height();
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<149>");
	gc_assign(m_frames,Array<c_Frame* >(t_nframes));
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<150>");
	for(int t_i=0;t_i<t_nframes;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<151>");
		gc_assign(m_frames.At(t_i),(new c_Frame)->m_new(t_i*m_width,0));
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<154>");
	p_ApplyFlags(t_iflags);
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<155>");
	return this;
}
c_Image* c_Image::p_Init2(gxtkSurface* t_surf,int t_x,int t_y,int t_iwidth,int t_iheight,int t_nframes,int t_iflags,c_Image* t_src,int t_srcx,int t_srcy,int t_srcw,int t_srch){
	DBG_ENTER("Image.Init")
	c_Image *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_surf,"surf")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_iwidth,"iwidth")
	DBG_LOCAL(t_iheight,"iheight")
	DBG_LOCAL(t_nframes,"nframes")
	DBG_LOCAL(t_iflags,"iflags")
	DBG_LOCAL(t_src,"src")
	DBG_LOCAL(t_srcx,"srcx")
	DBG_LOCAL(t_srcy,"srcy")
	DBG_LOCAL(t_srcw,"srcw")
	DBG_LOCAL(t_srch,"srch")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<159>");
	if((m_surface)!=0){
		DBG_BLOCK();
		bbError(String(L"Image already initialized",25));
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<160>");
	gc_assign(m_surface,t_surf);
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<161>");
	gc_assign(m_source,t_src);
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<163>");
	m_width=t_iwidth;
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<164>");
	m_height=t_iheight;
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<166>");
	gc_assign(m_frames,Array<c_Frame* >(t_nframes));
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<168>");
	int t_ix=t_x;
	int t_iy=t_y;
	DBG_LOCAL(t_ix,"ix")
	DBG_LOCAL(t_iy,"iy")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<170>");
	for(int t_i=0;t_i<t_nframes;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<171>");
		if(t_ix+m_width>t_srcw){
			DBG_BLOCK();
			DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<172>");
			t_ix=0;
			DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<173>");
			t_iy+=m_height;
		}
		DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<175>");
		if(t_ix+m_width>t_srcw || t_iy+m_height>t_srch){
			DBG_BLOCK();
			DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<176>");
			bbError(String(L"Image frame outside surface",27));
		}
		DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<178>");
		gc_assign(m_frames.At(t_i),(new c_Frame)->m_new(t_ix+t_srcx,t_iy+t_srcy));
		DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<179>");
		t_ix+=m_width;
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<182>");
	p_ApplyFlags(t_iflags);
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<183>");
	return this;
}
int c_Image::p_WritePixels(Array<int > t_pixels,int t_x,int t_y,int t_width,int t_height,int t_offset,int t_pitch){
	DBG_ENTER("Image.WritePixels")
	c_Image *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_pixels,"pixels")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_width,"width")
	DBG_LOCAL(t_height,"height")
	DBG_LOCAL(t_offset,"offset")
	DBG_LOCAL(t_pitch,"pitch")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<127>");
	if(!((t_pitch)!=0)){
		DBG_BLOCK();
		t_pitch=t_width;
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<130>");
	int t_w=this->m_width;
	DBG_LOCAL(t_w,"w")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<131>");
	if((m_flags&2)!=0){
		DBG_BLOCK();
		t_w+=2;
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<132>");
	int t_h=this->m_height;
	DBG_LOCAL(t_h,"h")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<133>");
	if((m_flags&4)!=0){
		DBG_BLOCK();
		t_h+=2;
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<134>");
	if(t_x<0 || t_y<0 || t_x+t_width>t_w || t_y+t_height>t_h){
		DBG_BLOCK();
		bbError(String(L"Invalid pixel rectangle",23));
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<135>");
	if(t_offset<0 || t_pitch<0 || t_offset+(t_height-1)*t_pitch+t_width>t_pixels.Length()){
		DBG_BLOCK();
		bbError(String(L"Invalid array rectangle",23));
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<137>");
	bb_graphics_device->WritePixels2(m_surface,t_pixels,t_x,t_y,t_width,t_height,t_offset,t_pitch);
	return 0;
}
int c_Image::p_Width(){
	DBG_ENTER("Image.Width")
	c_Image *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<81>");
	return m_width;
}
int c_Image::p_Height(){
	DBG_ENTER("Image.Height")
	c_Image *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<85>");
	return m_height;
}
int c_Image::p_Frames(){
	DBG_ENTER("Image.Frames")
	c_Image *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<93>");
	int t_=m_frames.Length();
	return t_;
}
void c_Image::mark(){
	Object::mark();
	gc_mark_q(m_surface);
	gc_mark_q(m_frames);
	gc_mark_q(m_source);
}
String c_Image::debug(){
	String t="(Image)\n";
	t+=dbg_decl("DefaultFlags",&c_Image::m_DefaultFlags);
	t+=dbg_decl("source",&m_source);
	t+=dbg_decl("surface",&m_surface);
	t+=dbg_decl("width",&m_width);
	t+=dbg_decl("height",&m_height);
	t+=dbg_decl("flags",&m_flags);
	t+=dbg_decl("frames",&m_frames);
	t+=dbg_decl("tx",&m_tx);
	t+=dbg_decl("ty",&m_ty);
	return t;
}
c_GraphicsContext::c_GraphicsContext(){
	m_defaultFont=0;
	m_font=0;
	m_firstChar=0;
	m_matrixSp=0;
	m_ix=FLOAT(1.0);
	m_iy=FLOAT(.0);
	m_jx=FLOAT(.0);
	m_jy=FLOAT(1.0);
	m_tx=FLOAT(.0);
	m_ty=FLOAT(.0);
	m_tformed=0;
	m_matDirty=0;
	m_color_r=FLOAT(.0);
	m_color_g=FLOAT(.0);
	m_color_b=FLOAT(.0);
	m_alpha=FLOAT(.0);
	m_blend=0;
	m_scissor_x=FLOAT(.0);
	m_scissor_y=FLOAT(.0);
	m_scissor_width=FLOAT(.0);
	m_scissor_height=FLOAT(.0);
	m_matrixStack=Array<Float >(192);
}
c_GraphicsContext* c_GraphicsContext::m_new(){
	DBG_ENTER("GraphicsContext.new")
	c_GraphicsContext *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<29>");
	return this;
}
int c_GraphicsContext::p_Validate(){
	DBG_ENTER("GraphicsContext.Validate")
	c_GraphicsContext *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<40>");
	if((m_matDirty)!=0){
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<41>");
		bb_graphics_renderDevice->SetMatrix(bb_graphics_context->m_ix,bb_graphics_context->m_iy,bb_graphics_context->m_jx,bb_graphics_context->m_jy,bb_graphics_context->m_tx,bb_graphics_context->m_ty);
		DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<42>");
		m_matDirty=0;
	}
	return 0;
}
void c_GraphicsContext::mark(){
	Object::mark();
	gc_mark_q(m_defaultFont);
	gc_mark_q(m_font);
	gc_mark_q(m_matrixStack);
}
String c_GraphicsContext::debug(){
	String t="(GraphicsContext)\n";
	t+=dbg_decl("color_r",&m_color_r);
	t+=dbg_decl("color_g",&m_color_g);
	t+=dbg_decl("color_b",&m_color_b);
	t+=dbg_decl("alpha",&m_alpha);
	t+=dbg_decl("blend",&m_blend);
	t+=dbg_decl("ix",&m_ix);
	t+=dbg_decl("iy",&m_iy);
	t+=dbg_decl("jx",&m_jx);
	t+=dbg_decl("jy",&m_jy);
	t+=dbg_decl("tx",&m_tx);
	t+=dbg_decl("ty",&m_ty);
	t+=dbg_decl("tformed",&m_tformed);
	t+=dbg_decl("matDirty",&m_matDirty);
	t+=dbg_decl("scissor_x",&m_scissor_x);
	t+=dbg_decl("scissor_y",&m_scissor_y);
	t+=dbg_decl("scissor_width",&m_scissor_width);
	t+=dbg_decl("scissor_height",&m_scissor_height);
	t+=dbg_decl("matrixStack",&m_matrixStack);
	t+=dbg_decl("matrixSp",&m_matrixSp);
	t+=dbg_decl("font",&m_font);
	t+=dbg_decl("firstChar",&m_firstChar);
	t+=dbg_decl("defaultFont",&m_defaultFont);
	return t;
}
c_GraphicsContext* bb_graphics_context;
String bb_data_FixDataPath(String t_path){
	DBG_ENTER("FixDataPath")
	DBG_LOCAL(t_path,"path")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/data.monkey<7>");
	int t_i=t_path.Find(String(L":/",2),0);
	DBG_LOCAL(t_i,"i")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/data.monkey<8>");
	if(t_i!=-1 && t_path.Find(String(L"/",1),0)==t_i+1){
		DBG_BLOCK();
		return t_path;
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/data.monkey<9>");
	if(t_path.StartsWith(String(L"./",2)) || t_path.StartsWith(String(L"/",1))){
		DBG_BLOCK();
		return t_path;
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/data.monkey<10>");
	String t_=String(L"monkey://data/",14)+t_path;
	return t_;
}
c_Frame::c_Frame(){
	m_x=0;
	m_y=0;
}
c_Frame* c_Frame::m_new(int t_x,int t_y){
	DBG_ENTER("Frame.new")
	c_Frame *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<23>");
	this->m_x=t_x;
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<24>");
	this->m_y=t_y;
	return this;
}
c_Frame* c_Frame::m_new2(){
	DBG_ENTER("Frame.new")
	c_Frame *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<18>");
	return this;
}
void c_Frame::mark(){
	Object::mark();
}
String c_Frame::debug(){
	String t="(Frame)\n";
	t+=dbg_decl("x",&m_x);
	t+=dbg_decl("y",&m_y);
	return t;
}
c_Image* bb_graphics_LoadImage(String t_path,int t_frameCount,int t_flags){
	DBG_ENTER("LoadImage")
	DBG_LOCAL(t_path,"path")
	DBG_LOCAL(t_frameCount,"frameCount")
	DBG_LOCAL(t_flags,"flags")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<239>");
	gxtkSurface* t_surf=bb_graphics_device->LoadSurface(bb_data_FixDataPath(t_path));
	DBG_LOCAL(t_surf,"surf")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<240>");
	if((t_surf)!=0){
		DBG_BLOCK();
		c_Image* t_=((new c_Image)->m_new())->p_Init(t_surf,t_frameCount,t_flags);
		return t_;
	}
	return 0;
}
c_Image* bb_graphics_LoadImage2(String t_path,int t_frameWidth,int t_frameHeight,int t_frameCount,int t_flags){
	DBG_ENTER("LoadImage")
	DBG_LOCAL(t_path,"path")
	DBG_LOCAL(t_frameWidth,"frameWidth")
	DBG_LOCAL(t_frameHeight,"frameHeight")
	DBG_LOCAL(t_frameCount,"frameCount")
	DBG_LOCAL(t_flags,"flags")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<244>");
	gxtkSurface* t_surf=bb_graphics_device->LoadSurface(bb_data_FixDataPath(t_path));
	DBG_LOCAL(t_surf,"surf")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<245>");
	if((t_surf)!=0){
		DBG_BLOCK();
		c_Image* t_=((new c_Image)->m_new())->p_Init2(t_surf,0,0,t_frameWidth,t_frameHeight,t_frameCount,t_flags,0,0,0,t_surf->Width(),t_surf->Height());
		return t_;
	}
	return 0;
}
int bb_graphics_SetFont(c_Image* t_font,int t_firstChar){
	DBG_ENTER("SetFont")
	DBG_LOCAL(t_font,"font")
	DBG_LOCAL(t_firstChar,"firstChar")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<548>");
	if(!((t_font)!=0)){
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<549>");
		if(!((bb_graphics_context->m_defaultFont)!=0)){
			DBG_BLOCK();
			DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<550>");
			gc_assign(bb_graphics_context->m_defaultFont,bb_graphics_LoadImage(String(L"mojo_font.png",13),96,2));
		}
		DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<552>");
		t_font=bb_graphics_context->m_defaultFont;
		DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<553>");
		t_firstChar=32;
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<555>");
	gc_assign(bb_graphics_context->m_font,t_font);
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<556>");
	bb_graphics_context->m_firstChar=t_firstChar;
	return 0;
}
gxtkAudio* bb_audio_device;
int bb_audio_SetAudioDevice(gxtkAudio* t_dev){
	DBG_ENTER("SetAudioDevice")
	DBG_LOCAL(t_dev,"dev")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/audio.monkey<22>");
	gc_assign(bb_audio_device,t_dev);
	return 0;
}
c_InputDevice::c_InputDevice(){
	m__joyStates=Array<c_JoyState* >(4);
	m__keyDown=Array<bool >(512);
	m__keyHitPut=0;
	m__keyHitQueue=Array<int >(33);
	m__keyHit=Array<int >(512);
	m__charGet=0;
	m__charPut=0;
	m__charQueue=Array<int >(32);
	m__mouseX=FLOAT(.0);
	m__mouseY=FLOAT(.0);
	m__touchX=Array<Float >(32);
	m__touchY=Array<Float >(32);
	m__accelX=FLOAT(.0);
	m__accelY=FLOAT(.0);
	m__accelZ=FLOAT(.0);
}
c_InputDevice* c_InputDevice::m_new(){
	DBG_ENTER("InputDevice.new")
	c_InputDevice *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<26>");
	for(int t_i=0;t_i<4;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<27>");
		gc_assign(m__joyStates.At(t_i),(new c_JoyState)->m_new());
	}
	return this;
}
void c_InputDevice::p_PutKeyHit(int t_key){
	DBG_ENTER("InputDevice.PutKeyHit")
	c_InputDevice *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<237>");
	if(m__keyHitPut==m__keyHitQueue.Length()){
		DBG_BLOCK();
		return;
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<238>");
	m__keyHit.At(t_key)+=1;
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<239>");
	m__keyHitQueue.At(m__keyHitPut)=t_key;
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<240>");
	m__keyHitPut+=1;
}
void c_InputDevice::p_BeginUpdate(){
	DBG_ENTER("InputDevice.BeginUpdate")
	c_InputDevice *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<189>");
	for(int t_i=0;t_i<4;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<190>");
		c_JoyState* t_state=m__joyStates.At(t_i);
		DBG_LOCAL(t_state,"state")
		DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<191>");
		if(!BBGame::Game()->PollJoystick(t_i,t_state->m_joyx,t_state->m_joyy,t_state->m_joyz,t_state->m_buttons)){
			DBG_BLOCK();
			break;
		}
		DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<192>");
		for(int t_j=0;t_j<32;t_j=t_j+1){
			DBG_BLOCK();
			DBG_LOCAL(t_j,"j")
			DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<193>");
			int t_key=256+t_i*32+t_j;
			DBG_LOCAL(t_key,"key")
			DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<194>");
			if(t_state->m_buttons.At(t_j)){
				DBG_BLOCK();
				DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<195>");
				if(!m__keyDown.At(t_key)){
					DBG_BLOCK();
					DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<196>");
					m__keyDown.At(t_key)=true;
					DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<197>");
					p_PutKeyHit(t_key);
				}
			}else{
				DBG_BLOCK();
				DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<200>");
				m__keyDown.At(t_key)=false;
			}
		}
	}
}
void c_InputDevice::p_EndUpdate(){
	DBG_ENTER("InputDevice.EndUpdate")
	c_InputDevice *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<207>");
	for(int t_i=0;t_i<m__keyHitPut;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<208>");
		m__keyHit.At(m__keyHitQueue.At(t_i))=0;
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<210>");
	m__keyHitPut=0;
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<211>");
	m__charGet=0;
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<212>");
	m__charPut=0;
}
void c_InputDevice::p_KeyEvent(int t_event,int t_data){
	DBG_ENTER("InputDevice.KeyEvent")
	c_InputDevice *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_event,"event")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<111>");
	int t_1=t_event;
	DBG_LOCAL(t_1,"1")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<112>");
	if(t_1==1){
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<113>");
		if(!m__keyDown.At(t_data)){
			DBG_BLOCK();
			DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<114>");
			m__keyDown.At(t_data)=true;
			DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<115>");
			p_PutKeyHit(t_data);
			DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<116>");
			if(t_data==1){
				DBG_BLOCK();
				DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<117>");
				m__keyDown.At(384)=true;
				DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<118>");
				p_PutKeyHit(384);
			}else{
				DBG_BLOCK();
				DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<119>");
				if(t_data==384){
					DBG_BLOCK();
					DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<120>");
					m__keyDown.At(1)=true;
					DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<121>");
					p_PutKeyHit(1);
				}
			}
		}
	}else{
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<124>");
		if(t_1==2){
			DBG_BLOCK();
			DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<125>");
			if(m__keyDown.At(t_data)){
				DBG_BLOCK();
				DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<126>");
				m__keyDown.At(t_data)=false;
				DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<127>");
				if(t_data==1){
					DBG_BLOCK();
					DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<128>");
					m__keyDown.At(384)=false;
				}else{
					DBG_BLOCK();
					DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<129>");
					if(t_data==384){
						DBG_BLOCK();
						DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<130>");
						m__keyDown.At(1)=false;
					}
				}
			}
		}else{
			DBG_BLOCK();
			DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<133>");
			if(t_1==3){
				DBG_BLOCK();
				DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<134>");
				if(m__charPut<m__charQueue.Length()){
					DBG_BLOCK();
					DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<135>");
					m__charQueue.At(m__charPut)=t_data;
					DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<136>");
					m__charPut+=1;
				}
			}
		}
	}
}
void c_InputDevice::p_MouseEvent(int t_event,int t_data,Float t_x,Float t_y){
	DBG_ENTER("InputDevice.MouseEvent")
	c_InputDevice *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_event,"event")
	DBG_LOCAL(t_data,"data")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<142>");
	int t_2=t_event;
	DBG_LOCAL(t_2,"2")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<143>");
	if(t_2==4){
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<144>");
		p_KeyEvent(1,1+t_data);
	}else{
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<145>");
		if(t_2==5){
			DBG_BLOCK();
			DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<146>");
			p_KeyEvent(2,1+t_data);
			return;
		}else{
			DBG_BLOCK();
			DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<148>");
			if(t_2==6){
				DBG_BLOCK();
			}else{
				DBG_BLOCK();
				return;
			}
		}
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<152>");
	m__mouseX=t_x;
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<153>");
	m__mouseY=t_y;
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<154>");
	m__touchX.At(0)=t_x;
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<155>");
	m__touchY.At(0)=t_y;
}
void c_InputDevice::p_TouchEvent(int t_event,int t_data,Float t_x,Float t_y){
	DBG_ENTER("InputDevice.TouchEvent")
	c_InputDevice *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_event,"event")
	DBG_LOCAL(t_data,"data")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<159>");
	int t_3=t_event;
	DBG_LOCAL(t_3,"3")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<160>");
	if(t_3==7){
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<161>");
		p_KeyEvent(1,384+t_data);
	}else{
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<162>");
		if(t_3==8){
			DBG_BLOCK();
			DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<163>");
			p_KeyEvent(2,384+t_data);
			return;
		}else{
			DBG_BLOCK();
			DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<165>");
			if(t_3==9){
				DBG_BLOCK();
			}else{
				DBG_BLOCK();
				return;
			}
		}
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<169>");
	m__touchX.At(t_data)=t_x;
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<170>");
	m__touchY.At(t_data)=t_y;
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<171>");
	if(t_data==0){
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<172>");
		m__mouseX=t_x;
		DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<173>");
		m__mouseY=t_y;
	}
}
void c_InputDevice::p_MotionEvent(int t_event,int t_data,Float t_x,Float t_y,Float t_z){
	DBG_ENTER("InputDevice.MotionEvent")
	c_InputDevice *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_event,"event")
	DBG_LOCAL(t_data,"data")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_z,"z")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<178>");
	int t_4=t_event;
	DBG_LOCAL(t_4,"4")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<179>");
	if(t_4==10){
		DBG_BLOCK();
	}else{
		DBG_BLOCK();
		return;
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<183>");
	m__accelX=t_x;
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<184>");
	m__accelY=t_y;
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<185>");
	m__accelZ=t_z;
}
int c_InputDevice::p_KeyHit(int t_key){
	DBG_ENTER("InputDevice.KeyHit")
	c_InputDevice *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<52>");
	if(t_key>0 && t_key<512){
		DBG_BLOCK();
		return m__keyHit.At(t_key);
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<53>");
	return 0;
}
int c_InputDevice::p_GetChar(){
	DBG_ENTER("InputDevice.GetChar")
	c_InputDevice *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<57>");
	if(m__charGet==m__charPut){
		DBG_BLOCK();
		return 0;
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<58>");
	int t_chr=m__charQueue.At(m__charGet);
	DBG_LOCAL(t_chr,"chr")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<59>");
	m__charGet+=1;
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<60>");
	return t_chr;
}
bool c_InputDevice::p_KeyDown(int t_key){
	DBG_ENTER("InputDevice.KeyDown")
	c_InputDevice *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<47>");
	if(t_key>0 && t_key<512){
		DBG_BLOCK();
		return m__keyDown.At(t_key);
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<48>");
	return false;
}
void c_InputDevice::mark(){
	Object::mark();
	gc_mark_q(m__joyStates);
	gc_mark_q(m__keyDown);
	gc_mark_q(m__keyHitQueue);
	gc_mark_q(m__keyHit);
	gc_mark_q(m__charQueue);
	gc_mark_q(m__touchX);
	gc_mark_q(m__touchY);
}
String c_InputDevice::debug(){
	String t="(InputDevice)\n";
	t+=dbg_decl("_keyDown",&m__keyDown);
	t+=dbg_decl("_keyHit",&m__keyHit);
	t+=dbg_decl("_keyHitQueue",&m__keyHitQueue);
	t+=dbg_decl("_keyHitPut",&m__keyHitPut);
	t+=dbg_decl("_charQueue",&m__charQueue);
	t+=dbg_decl("_charPut",&m__charPut);
	t+=dbg_decl("_charGet",&m__charGet);
	t+=dbg_decl("_mouseX",&m__mouseX);
	t+=dbg_decl("_mouseY",&m__mouseY);
	t+=dbg_decl("_touchX",&m__touchX);
	t+=dbg_decl("_touchY",&m__touchY);
	t+=dbg_decl("_accelX",&m__accelX);
	t+=dbg_decl("_accelY",&m__accelY);
	t+=dbg_decl("_accelZ",&m__accelZ);
	t+=dbg_decl("_joyStates",&m__joyStates);
	return t;
}
c_JoyState::c_JoyState(){
	m_joyx=Array<Float >(2);
	m_joyy=Array<Float >(2);
	m_joyz=Array<Float >(2);
	m_buttons=Array<bool >(32);
}
c_JoyState* c_JoyState::m_new(){
	DBG_ENTER("JoyState.new")
	c_JoyState *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/inputdevice.monkey<14>");
	return this;
}
void c_JoyState::mark(){
	Object::mark();
	gc_mark_q(m_joyx);
	gc_mark_q(m_joyy);
	gc_mark_q(m_joyz);
	gc_mark_q(m_buttons);
}
String c_JoyState::debug(){
	String t="(JoyState)\n";
	t+=dbg_decl("joyx",&m_joyx);
	t+=dbg_decl("joyy",&m_joyy);
	t+=dbg_decl("joyz",&m_joyz);
	t+=dbg_decl("buttons",&m_buttons);
	return t;
}
c_InputDevice* bb_input_device;
int bb_input_SetInputDevice(c_InputDevice* t_dev){
	DBG_ENTER("SetInputDevice")
	DBG_LOCAL(t_dev,"dev")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/input.monkey<22>");
	gc_assign(bb_input_device,t_dev);
	return 0;
}
int bb_app__devWidth;
int bb_app__devHeight;
void bb_app_ValidateDeviceWindow(bool t_notifyApp){
	DBG_ENTER("ValidateDeviceWindow")
	DBG_LOCAL(t_notifyApp,"notifyApp")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<57>");
	int t_w=bb_app__game->GetDeviceWidth();
	DBG_LOCAL(t_w,"w")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<58>");
	int t_h=bb_app__game->GetDeviceHeight();
	DBG_LOCAL(t_h,"h")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<59>");
	if(t_w==bb_app__devWidth && t_h==bb_app__devHeight){
		DBG_BLOCK();
		return;
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<60>");
	bb_app__devWidth=t_w;
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<61>");
	bb_app__devHeight=t_h;
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<62>");
	if(t_notifyApp){
		DBG_BLOCK();
		bb_app__app->p_OnResize();
	}
}
c_DisplayMode::c_DisplayMode(){
	m__width=0;
	m__height=0;
}
c_DisplayMode* c_DisplayMode::m_new(int t_width,int t_height){
	DBG_ENTER("DisplayMode.new")
	c_DisplayMode *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_width,"width")
	DBG_LOCAL(t_height,"height")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<192>");
	m__width=t_width;
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<193>");
	m__height=t_height;
	return this;
}
c_DisplayMode* c_DisplayMode::m_new2(){
	DBG_ENTER("DisplayMode.new")
	c_DisplayMode *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<189>");
	return this;
}
void c_DisplayMode::mark(){
	Object::mark();
}
String c_DisplayMode::debug(){
	String t="(DisplayMode)\n";
	t+=dbg_decl("_width",&m__width);
	t+=dbg_decl("_height",&m__height);
	return t;
}
c_Map::c_Map(){
	m_root=0;
}
c_Map* c_Map::m_new(){
	DBG_ENTER("Map.new")
	c_Map *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<7>");
	return this;
}
c_Node* c_Map::p_FindNode(int t_key){
	DBG_ENTER("Map.FindNode")
	c_Map *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<157>");
	c_Node* t_node=m_root;
	DBG_LOCAL(t_node,"node")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<159>");
	while((t_node)!=0){
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<160>");
		int t_cmp=p_Compare(t_key,t_node->m_key);
		DBG_LOCAL(t_cmp,"cmp")
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<161>");
		if(t_cmp>0){
			DBG_BLOCK();
			DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<162>");
			t_node=t_node->m_right;
		}else{
			DBG_BLOCK();
			DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<163>");
			if(t_cmp<0){
				DBG_BLOCK();
				DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<164>");
				t_node=t_node->m_left;
			}else{
				DBG_BLOCK();
				DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<166>");
				return t_node;
			}
		}
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<169>");
	return t_node;
}
bool c_Map::p_Contains(int t_key){
	DBG_ENTER("Map.Contains")
	c_Map *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<25>");
	bool t_=p_FindNode(t_key)!=0;
	return t_;
}
int c_Map::p_RotateLeft(c_Node* t_node){
	DBG_ENTER("Map.RotateLeft")
	c_Map *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_node,"node")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<251>");
	c_Node* t_child=t_node->m_right;
	DBG_LOCAL(t_child,"child")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<252>");
	gc_assign(t_node->m_right,t_child->m_left);
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<253>");
	if((t_child->m_left)!=0){
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<254>");
		gc_assign(t_child->m_left->m_parent,t_node);
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<256>");
	gc_assign(t_child->m_parent,t_node->m_parent);
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<257>");
	if((t_node->m_parent)!=0){
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<258>");
		if(t_node==t_node->m_parent->m_left){
			DBG_BLOCK();
			DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<259>");
			gc_assign(t_node->m_parent->m_left,t_child);
		}else{
			DBG_BLOCK();
			DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<261>");
			gc_assign(t_node->m_parent->m_right,t_child);
		}
	}else{
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<264>");
		gc_assign(m_root,t_child);
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<266>");
	gc_assign(t_child->m_left,t_node);
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<267>");
	gc_assign(t_node->m_parent,t_child);
	return 0;
}
int c_Map::p_RotateRight(c_Node* t_node){
	DBG_ENTER("Map.RotateRight")
	c_Map *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_node,"node")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<271>");
	c_Node* t_child=t_node->m_left;
	DBG_LOCAL(t_child,"child")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<272>");
	gc_assign(t_node->m_left,t_child->m_right);
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<273>");
	if((t_child->m_right)!=0){
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<274>");
		gc_assign(t_child->m_right->m_parent,t_node);
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<276>");
	gc_assign(t_child->m_parent,t_node->m_parent);
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<277>");
	if((t_node->m_parent)!=0){
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<278>");
		if(t_node==t_node->m_parent->m_right){
			DBG_BLOCK();
			DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<279>");
			gc_assign(t_node->m_parent->m_right,t_child);
		}else{
			DBG_BLOCK();
			DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<281>");
			gc_assign(t_node->m_parent->m_left,t_child);
		}
	}else{
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<284>");
		gc_assign(m_root,t_child);
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<286>");
	gc_assign(t_child->m_right,t_node);
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<287>");
	gc_assign(t_node->m_parent,t_child);
	return 0;
}
int c_Map::p_InsertFixup(c_Node* t_node){
	DBG_ENTER("Map.InsertFixup")
	c_Map *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_node,"node")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<212>");
	while(((t_node->m_parent)!=0) && t_node->m_parent->m_color==-1 && ((t_node->m_parent->m_parent)!=0)){
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<213>");
		if(t_node->m_parent==t_node->m_parent->m_parent->m_left){
			DBG_BLOCK();
			DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<214>");
			c_Node* t_uncle=t_node->m_parent->m_parent->m_right;
			DBG_LOCAL(t_uncle,"uncle")
			DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<215>");
			if(((t_uncle)!=0) && t_uncle->m_color==-1){
				DBG_BLOCK();
				DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<216>");
				t_node->m_parent->m_color=1;
				DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<217>");
				t_uncle->m_color=1;
				DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<218>");
				t_uncle->m_parent->m_color=-1;
				DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<219>");
				t_node=t_uncle->m_parent;
			}else{
				DBG_BLOCK();
				DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<221>");
				if(t_node==t_node->m_parent->m_right){
					DBG_BLOCK();
					DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<222>");
					t_node=t_node->m_parent;
					DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<223>");
					p_RotateLeft(t_node);
				}
				DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<225>");
				t_node->m_parent->m_color=1;
				DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<226>");
				t_node->m_parent->m_parent->m_color=-1;
				DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<227>");
				p_RotateRight(t_node->m_parent->m_parent);
			}
		}else{
			DBG_BLOCK();
			DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<230>");
			c_Node* t_uncle2=t_node->m_parent->m_parent->m_left;
			DBG_LOCAL(t_uncle2,"uncle")
			DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<231>");
			if(((t_uncle2)!=0) && t_uncle2->m_color==-1){
				DBG_BLOCK();
				DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<232>");
				t_node->m_parent->m_color=1;
				DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<233>");
				t_uncle2->m_color=1;
				DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<234>");
				t_uncle2->m_parent->m_color=-1;
				DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<235>");
				t_node=t_uncle2->m_parent;
			}else{
				DBG_BLOCK();
				DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<237>");
				if(t_node==t_node->m_parent->m_left){
					DBG_BLOCK();
					DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<238>");
					t_node=t_node->m_parent;
					DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<239>");
					p_RotateRight(t_node);
				}
				DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<241>");
				t_node->m_parent->m_color=1;
				DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<242>");
				t_node->m_parent->m_parent->m_color=-1;
				DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<243>");
				p_RotateLeft(t_node->m_parent->m_parent);
			}
		}
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<247>");
	m_root->m_color=1;
	return 0;
}
bool c_Map::p_Set(int t_key,c_DisplayMode* t_value){
	DBG_ENTER("Map.Set")
	c_Map *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<29>");
	c_Node* t_node=m_root;
	DBG_LOCAL(t_node,"node")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<30>");
	c_Node* t_parent=0;
	int t_cmp=0;
	DBG_LOCAL(t_parent,"parent")
	DBG_LOCAL(t_cmp,"cmp")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<32>");
	while((t_node)!=0){
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<33>");
		t_parent=t_node;
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<34>");
		t_cmp=p_Compare(t_key,t_node->m_key);
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<35>");
		if(t_cmp>0){
			DBG_BLOCK();
			DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<36>");
			t_node=t_node->m_right;
		}else{
			DBG_BLOCK();
			DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<37>");
			if(t_cmp<0){
				DBG_BLOCK();
				DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<38>");
				t_node=t_node->m_left;
			}else{
				DBG_BLOCK();
				DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<40>");
				gc_assign(t_node->m_value,t_value);
				DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<41>");
				return false;
			}
		}
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<45>");
	t_node=(new c_Node)->m_new(t_key,t_value,-1,t_parent);
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<47>");
	if((t_parent)!=0){
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<48>");
		if(t_cmp>0){
			DBG_BLOCK();
			DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<49>");
			gc_assign(t_parent->m_right,t_node);
		}else{
			DBG_BLOCK();
			DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<51>");
			gc_assign(t_parent->m_left,t_node);
		}
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<53>");
		p_InsertFixup(t_node);
	}else{
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<55>");
		gc_assign(m_root,t_node);
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<57>");
	return true;
}
bool c_Map::p_Insert(int t_key,c_DisplayMode* t_value){
	DBG_ENTER("Map.Insert")
	c_Map *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<146>");
	bool t_=p_Set(t_key,t_value);
	return t_;
}
void c_Map::mark(){
	Object::mark();
	gc_mark_q(m_root);
}
String c_Map::debug(){
	String t="(Map)\n";
	t+=dbg_decl("root",&m_root);
	return t;
}
c_IntMap::c_IntMap(){
}
c_IntMap* c_IntMap::m_new(){
	DBG_ENTER("IntMap.new")
	c_IntMap *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<534>");
	c_Map::m_new();
	return this;
}
int c_IntMap::p_Compare(int t_lhs,int t_rhs){
	DBG_ENTER("IntMap.Compare")
	c_IntMap *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_lhs,"lhs")
	DBG_LOCAL(t_rhs,"rhs")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<537>");
	int t_=t_lhs-t_rhs;
	return t_;
}
void c_IntMap::mark(){
	c_Map::mark();
}
String c_IntMap::debug(){
	String t="(IntMap)\n";
	t=c_Map::debug()+t;
	return t;
}
c_Stack::c_Stack(){
	m_data=Array<c_DisplayMode* >();
	m_length=0;
}
c_Stack* c_Stack::m_new(){
	DBG_ENTER("Stack.new")
	c_Stack *self=this;
	DBG_LOCAL(self,"Self")
	return this;
}
c_Stack* c_Stack::m_new2(Array<c_DisplayMode* > t_data){
	DBG_ENTER("Stack.new")
	c_Stack *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<13>");
	gc_assign(this->m_data,t_data.Slice(0));
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<14>");
	this->m_length=t_data.Length();
	return this;
}
void c_Stack::p_Push(c_DisplayMode* t_value){
	DBG_ENTER("Stack.Push")
	c_Stack *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<71>");
	if(m_length==m_data.Length()){
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<72>");
		gc_assign(m_data,m_data.Resize(m_length*2+10));
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<74>");
	gc_assign(m_data.At(m_length),t_value);
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<75>");
	m_length+=1;
}
void c_Stack::p_Push2(Array<c_DisplayMode* > t_values,int t_offset,int t_count){
	DBG_ENTER("Stack.Push")
	c_Stack *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_values,"values")
	DBG_LOCAL(t_offset,"offset")
	DBG_LOCAL(t_count,"count")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<83>");
	for(int t_i=0;t_i<t_count;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<84>");
		p_Push(t_values.At(t_offset+t_i));
	}
}
void c_Stack::p_Push3(Array<c_DisplayMode* > t_values,int t_offset){
	DBG_ENTER("Stack.Push")
	c_Stack *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_values,"values")
	DBG_LOCAL(t_offset,"offset")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<79>");
	p_Push2(t_values,t_offset,t_values.Length()-t_offset);
}
Array<c_DisplayMode* > c_Stack::p_ToArray(){
	DBG_ENTER("Stack.ToArray")
	c_Stack *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<18>");
	Array<c_DisplayMode* > t_t=Array<c_DisplayMode* >(m_length);
	DBG_LOCAL(t_t,"t")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<19>");
	for(int t_i=0;t_i<m_length;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<20>");
		gc_assign(t_t.At(t_i),m_data.At(t_i));
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<22>");
	return t_t;
}
void c_Stack::mark(){
	Object::mark();
	gc_mark_q(m_data);
}
String c_Stack::debug(){
	String t="(Stack)\n";
	t+=dbg_decl("data",&m_data);
	t+=dbg_decl("length",&m_length);
	return t;
}
c_Node::c_Node(){
	m_key=0;
	m_right=0;
	m_left=0;
	m_value=0;
	m_color=0;
	m_parent=0;
}
c_Node* c_Node::m_new(int t_key,c_DisplayMode* t_value,int t_color,c_Node* t_parent){
	DBG_ENTER("Node.new")
	c_Node *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_key,"key")
	DBG_LOCAL(t_value,"value")
	DBG_LOCAL(t_color,"color")
	DBG_LOCAL(t_parent,"parent")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<364>");
	this->m_key=t_key;
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<365>");
	gc_assign(this->m_value,t_value);
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<366>");
	this->m_color=t_color;
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<367>");
	gc_assign(this->m_parent,t_parent);
	return this;
}
c_Node* c_Node::m_new2(){
	DBG_ENTER("Node.new")
	c_Node *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/map.monkey<361>");
	return this;
}
void c_Node::mark(){
	Object::mark();
	gc_mark_q(m_right);
	gc_mark_q(m_left);
	gc_mark_q(m_value);
	gc_mark_q(m_parent);
}
String c_Node::debug(){
	String t="(Node)\n";
	t+=dbg_decl("key",&m_key);
	t+=dbg_decl("value",&m_value);
	t+=dbg_decl("color",&m_color);
	t+=dbg_decl("parent",&m_parent);
	t+=dbg_decl("left",&m_left);
	t+=dbg_decl("right",&m_right);
	return t;
}
Array<c_DisplayMode* > bb_app__displayModes;
c_DisplayMode* bb_app__desktopMode;
int bb_app_DeviceWidth(){
	DBG_ENTER("DeviceWidth")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<263>");
	return bb_app__devWidth;
}
int bb_app_DeviceHeight(){
	DBG_ENTER("DeviceHeight")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<267>");
	return bb_app__devHeight;
}
void bb_app_EnumDisplayModes(){
	DBG_ENTER("EnumDisplayModes")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<33>");
	Array<BBDisplayMode* > t_modes=bb_app__game->GetDisplayModes();
	DBG_LOCAL(t_modes,"modes")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<34>");
	c_IntMap* t_mmap=(new c_IntMap)->m_new();
	DBG_LOCAL(t_mmap,"mmap")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<35>");
	c_Stack* t_mstack=(new c_Stack)->m_new();
	DBG_LOCAL(t_mstack,"mstack")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<36>");
	for(int t_i=0;t_i<t_modes.Length();t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<37>");
		int t_w=t_modes.At(t_i)->width;
		DBG_LOCAL(t_w,"w")
		DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<38>");
		int t_h=t_modes.At(t_i)->height;
		DBG_LOCAL(t_h,"h")
		DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<39>");
		int t_size=t_w<<16|t_h;
		DBG_LOCAL(t_size,"size")
		DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<40>");
		if(t_mmap->p_Contains(t_size)){
			DBG_BLOCK();
		}else{
			DBG_BLOCK();
			DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<42>");
			c_DisplayMode* t_mode=(new c_DisplayMode)->m_new(t_modes.At(t_i)->width,t_modes.At(t_i)->height);
			DBG_LOCAL(t_mode,"mode")
			DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<43>");
			t_mmap->p_Insert(t_size,t_mode);
			DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<44>");
			t_mstack->p_Push(t_mode);
		}
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<47>");
	gc_assign(bb_app__displayModes,t_mstack->p_ToArray());
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<48>");
	BBDisplayMode* t_mode2=bb_app__game->GetDesktopMode();
	DBG_LOCAL(t_mode2,"mode")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<49>");
	if((t_mode2)!=0){
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<50>");
		gc_assign(bb_app__desktopMode,(new c_DisplayMode)->m_new(t_mode2->width,t_mode2->height));
	}else{
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<52>");
		gc_assign(bb_app__desktopMode,(new c_DisplayMode)->m_new(bb_app_DeviceWidth(),bb_app_DeviceHeight()));
	}
}
gxtkGraphics* bb_graphics_renderDevice;
int bb_graphics_SetMatrix(Float t_ix,Float t_iy,Float t_jx,Float t_jy,Float t_tx,Float t_ty){
	DBG_ENTER("SetMatrix")
	DBG_LOCAL(t_ix,"ix")
	DBG_LOCAL(t_iy,"iy")
	DBG_LOCAL(t_jx,"jx")
	DBG_LOCAL(t_jy,"jy")
	DBG_LOCAL(t_tx,"tx")
	DBG_LOCAL(t_ty,"ty")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<312>");
	bb_graphics_context->m_ix=t_ix;
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<313>");
	bb_graphics_context->m_iy=t_iy;
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<314>");
	bb_graphics_context->m_jx=t_jx;
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<315>");
	bb_graphics_context->m_jy=t_jy;
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<316>");
	bb_graphics_context->m_tx=t_tx;
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<317>");
	bb_graphics_context->m_ty=t_ty;
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<318>");
	bb_graphics_context->m_tformed=((t_ix!=FLOAT(1.0) || t_iy!=FLOAT(0.0) || t_jx!=FLOAT(0.0) || t_jy!=FLOAT(1.0) || t_tx!=FLOAT(0.0) || t_ty!=FLOAT(0.0))?1:0);
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<319>");
	bb_graphics_context->m_matDirty=1;
	return 0;
}
int bb_graphics_SetMatrix2(Array<Float > t_m){
	DBG_ENTER("SetMatrix")
	DBG_LOCAL(t_m,"m")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<308>");
	bb_graphics_SetMatrix(t_m.At(0),t_m.At(1),t_m.At(2),t_m.At(3),t_m.At(4),t_m.At(5));
	return 0;
}
int bb_graphics_SetColor(Float t_r,Float t_g,Float t_b){
	DBG_ENTER("SetColor")
	DBG_LOCAL(t_r,"r")
	DBG_LOCAL(t_g,"g")
	DBG_LOCAL(t_b,"b")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<254>");
	bb_graphics_context->m_color_r=t_r;
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<255>");
	bb_graphics_context->m_color_g=t_g;
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<256>");
	bb_graphics_context->m_color_b=t_b;
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<257>");
	bb_graphics_renderDevice->SetColor(t_r,t_g,t_b);
	return 0;
}
int bb_graphics_SetAlpha(Float t_alpha){
	DBG_ENTER("SetAlpha")
	DBG_LOCAL(t_alpha,"alpha")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<271>");
	bb_graphics_context->m_alpha=t_alpha;
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<272>");
	bb_graphics_renderDevice->SetAlpha(t_alpha);
	return 0;
}
int bb_graphics_SetBlend(int t_blend){
	DBG_ENTER("SetBlend")
	DBG_LOCAL(t_blend,"blend")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<280>");
	bb_graphics_context->m_blend=t_blend;
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<281>");
	bb_graphics_renderDevice->SetBlend(t_blend);
	return 0;
}
int bb_graphics_SetScissor(Float t_x,Float t_y,Float t_width,Float t_height){
	DBG_ENTER("SetScissor")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_width,"width")
	DBG_LOCAL(t_height,"height")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<289>");
	bb_graphics_context->m_scissor_x=t_x;
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<290>");
	bb_graphics_context->m_scissor_y=t_y;
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<291>");
	bb_graphics_context->m_scissor_width=t_width;
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<292>");
	bb_graphics_context->m_scissor_height=t_height;
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<293>");
	bb_graphics_renderDevice->SetScissor(int(t_x),int(t_y),int(t_width),int(t_height));
	return 0;
}
int bb_graphics_BeginRender(){
	DBG_ENTER("BeginRender")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<225>");
	gc_assign(bb_graphics_renderDevice,bb_graphics_device);
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<226>");
	bb_graphics_context->m_matrixSp=0;
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<227>");
	bb_graphics_SetMatrix(FLOAT(1.0),FLOAT(0.0),FLOAT(0.0),FLOAT(1.0),FLOAT(0.0),FLOAT(0.0));
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<228>");
	bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(255.0));
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<229>");
	bb_graphics_SetAlpha(FLOAT(1.0));
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<230>");
	bb_graphics_SetBlend(0);
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<231>");
	bb_graphics_SetScissor(FLOAT(0.0),FLOAT(0.0),Float(bb_app_DeviceWidth()),Float(bb_app_DeviceHeight()));
	return 0;
}
int bb_graphics_EndRender(){
	DBG_ENTER("EndRender")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<235>");
	bb_graphics_renderDevice=0;
	return 0;
}
c_BBGameEvent::c_BBGameEvent(){
}
void c_BBGameEvent::mark(){
	Object::mark();
}
String c_BBGameEvent::debug(){
	String t="(BBGameEvent)\n";
	return t;
}
void bb_app_EndApp(){
	DBG_ENTER("EndApp")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<259>");
	bbError(String());
}
c_Player::c_Player(){
	m_x=FLOAT(300.0);
	m_y=FLOAT(218.0);
	m_sprint_energy=500;
	m_Player_speed=FLOAT(1.2);
	m_sprint_multiplier=FLOAT(1.6666666666666667);
	m_up=false;
	m_down=false;
	m_left=false;
	m_right=false;
	m_yspeed=FLOAT(0.0);
	m_xspeed=FLOAT(0.0);
	m_x_old=FLOAT(.0);
	m_y_old=FLOAT(.0);
	m_sprint_regeneration=true;
	m_tick_sprint=99999999;
	m_increm=FLOAT(0.1);
	m_x_disp=FLOAT(.0);
	m_y_disp=FLOAT(.0);
	m_resultant_disp=FLOAT(.0);
	m_travel=FLOAT(0.0);
	m_footstep2_sound=bb_audio_LoadSound(String(L"footstep2.ogg",13));
	m_huff_sound=bb_audio_LoadSound(String(L"huff.ogg",8));
	m_sprite=bb_graphics_LoadImage(String(L"player.png",10),1,c_Image::m_DefaultFlags);
	m_sprite2=bb_graphics_LoadImage(String(L"playerleft.png",14),1,c_Image::m_DefaultFlags);
}
c_Player* c_Player::m_new(){
	DBG_ENTER("Player.new")
	c_Player *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<694>");
	return this;
}
int c_Player::p_Move(String t_GameState,int t_tick){
	DBG_ENTER("Player.Move")
	c_Player *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_GameState,"GameState")
	DBG_LOCAL(t_tick,"tick")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<727>");
	if(((bb_input_KeyDown(16))!=0) && m_sprint_energy>0 && t_GameState==String(L"PLAYING",7) && (bb_input_KeyDown(87)==1 || bb_input_KeyDown(65)==1 || bb_input_KeyDown(83)==1 || bb_input_KeyDown(68)==1)){
		DBG_BLOCK();
		m_Player_speed=m_Player_speed*m_sprint_multiplier;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<730>");
	if((bb_input_KeyDown(87))!=0){
		DBG_BLOCK();
		m_up=true;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<731>");
	if((bb_input_KeyDown(83))!=0){
		DBG_BLOCK();
		m_down=true;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<732>");
	if((bb_input_KeyDown(65))!=0){
		DBG_BLOCK();
		m_left=true;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<733>");
	if((bb_input_KeyDown(68))!=0){
		DBG_BLOCK();
		m_right=true;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<735>");
	if(m_left==true && m_right==true && m_down==true){
		DBG_BLOCK();
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<736>");
		m_right=false;
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<737>");
		m_left=false;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<740>");
	if(m_left==true && m_right==true && m_up==true){
		DBG_BLOCK();
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<741>");
		m_right=false;
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<742>");
		m_left=false;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<745>");
	if(m_up==true && m_down==true){
		DBG_BLOCK();
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<746>");
		m_down=false;
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<747>");
		m_up=false;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<750>");
	if(m_left==true && m_right==true){
		DBG_BLOCK();
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<751>");
		m_left=false;
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<752>");
		m_right=false;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<757>");
	if(m_up==true && m_left==true){
		DBG_BLOCK();
		m_Player_speed=m_Player_speed*FLOAT(0.70711);
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<758>");
	if(m_up==true && m_right==true){
		DBG_BLOCK();
		m_Player_speed=m_Player_speed*FLOAT(0.70711);
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<759>");
	if(m_down==true && m_left==true){
		DBG_BLOCK();
		m_Player_speed=m_Player_speed*FLOAT(0.70711);
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<760>");
	if(m_down==true && m_right==true){
		DBG_BLOCK();
		m_Player_speed=m_Player_speed*FLOAT(0.70711);
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<762>");
	if(m_yspeed>m_Player_speed){
		DBG_BLOCK();
		m_yspeed=m_Player_speed;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<763>");
	if(m_yspeed<-m_Player_speed){
		DBG_BLOCK();
		m_yspeed=-m_Player_speed;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<764>");
	if(m_xspeed>m_Player_speed){
		DBG_BLOCK();
		m_xspeed=m_Player_speed;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<765>");
	if(m_xspeed<-m_Player_speed){
		DBG_BLOCK();
		m_xspeed=-m_Player_speed;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<767>");
	m_x_old=m_x;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<768>");
	m_y_old=m_y;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<770>");
	if(m_sprint_regeneration==true && m_sprint_energy<500 && t_GameState==String(L"PLAYING",7)){
		DBG_BLOCK();
		m_sprint_energy+=1;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<772>");
	if(m_tick_sprint==t_tick && t_GameState==String(L"PLAYING",7)){
		DBG_BLOCK();
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<773>");
		m_tick_sprint=99999999;
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<774>");
		m_sprint_regeneration=true;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<777>");
	if(((bb_input_KeyDown(16))!=0) && m_sprint_energy>0 && (bb_input_KeyDown(87)==1 || bb_input_KeyDown(65)==1 || bb_input_KeyDown(83)==1 || bb_input_KeyDown(68)==1) && t_GameState==String(L"PLAYING",7)){
		DBG_BLOCK();
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<778>");
		m_sprint_energy-=1;
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<779>");
		m_sprint_regeneration=false;
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<780>");
		m_tick_sprint=t_tick;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<785>");
	if(m_up==true && m_yspeed>-m_Player_speed && t_GameState==String(L"PLAYING",7)){
		DBG_BLOCK();
		m_yspeed-=m_increm;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<786>");
	if(m_left==true && m_xspeed>-m_Player_speed && t_GameState==String(L"PLAYING",7)){
		DBG_BLOCK();
		m_xspeed-=m_increm;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<787>");
	if(m_down==true && m_yspeed<m_Player_speed && t_GameState==String(L"PLAYING",7)){
		DBG_BLOCK();
		m_yspeed+=m_increm;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<788>");
	if(m_right==true && m_xspeed<m_Player_speed && t_GameState==String(L"PLAYING",7)){
		DBG_BLOCK();
		m_xspeed+=m_increm;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<790>");
	if(m_up==false && m_down==false && m_yspeed>FLOAT(0.0)){
		DBG_BLOCK();
		m_yspeed-=m_increm;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<791>");
	if(m_up==false && m_down==false && m_yspeed<FLOAT(0.0)){
		DBG_BLOCK();
		m_yspeed+=m_increm;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<792>");
	if(m_left==false && m_right==false && m_xspeed>FLOAT(0.0)){
		DBG_BLOCK();
		m_xspeed-=m_increm;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<793>");
	if(m_left==false && m_right==false && m_xspeed<FLOAT(0.0)){
		DBG_BLOCK();
		m_xspeed+=m_increm;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<795>");
	if(-m_increm<m_xspeed && m_xspeed<m_increm){
		DBG_BLOCK();
		m_xspeed=FLOAT(0.0);
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<796>");
	if(-m_increm<m_yspeed && m_yspeed<m_increm){
		DBG_BLOCK();
		m_yspeed=FLOAT(0.0);
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<798>");
	m_y+=m_yspeed;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<799>");
	m_x+=m_xspeed;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<801>");
	if(m_x>=FLOAT(620.0)){
		DBG_BLOCK();
		m_x=FLOAT(620.0);
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<802>");
	if(m_x<=FLOAT(0.0)){
		DBG_BLOCK();
		m_x=FLOAT(0.0);
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<803>");
	if(m_y>=FLOAT(458.0)){
		DBG_BLOCK();
		m_y=FLOAT(458.0);
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<804>");
	if(m_y<=FLOAT(0.0)){
		DBG_BLOCK();
		m_y=FLOAT(0.0);
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<807>");
	m_x_disp=m_x-m_x_old;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<808>");
	m_y_disp=m_y-m_y_old;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<810>");
	m_resultant_disp=(Float)pow(m_x_disp*m_x_disp+m_y_disp*m_y_disp,FLOAT(0.5));
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<811>");
	m_travel+=m_resultant_disp;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<813>");
	if(((bb_input_KeyDown(16))!=0) && t_GameState==String(L"PLAYING",7) && m_sprint_energy>0 && (((bb_input_KeyDown(87))!=0) || ((bb_input_KeyDown(65))!=0) || ((bb_input_KeyDown(83))!=0) || ((bb_input_KeyDown(68))!=0))){
		DBG_BLOCK();
		m_travel+=m_increm;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<815>");
	if(m_travel>FLOAT(20.0) && t_GameState==String(L"PLAYING",7)){
		DBG_BLOCK();
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<821>");
		bb_audio_PlaySound(m_footstep2_sound,8,0);
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<822>");
		m_travel=FLOAT(0.0);
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<825>");
	if(((bb_input_KeyHit(16))!=0) && t_GameState==String(L"PLAYING",7)){
		DBG_BLOCK();
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<826>");
		bb_audio_PlaySound(m_huff_sound,9,0);
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<830>");
	m_Player_speed=FLOAT(1.2);
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<831>");
	m_up=false;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<832>");
	m_down=false;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<833>");
	m_left=false;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<834>");
	m_right=false;
	return 0;
}
int c_Player::p_give_x(){
	DBG_ENTER("Player.give_x")
	c_Player *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<840>");
	int t_=int(m_x);
	return t_;
}
int c_Player::p_give_y(){
	DBG_ENTER("Player.give_y")
	c_Player *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<844>");
	int t_=int(m_y);
	return t_;
}
int c_Player::p_give_sprint_energy(){
	DBG_ENTER("Player.give_sprint_energy")
	c_Player *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<849>");
	return m_sprint_energy;
}
void c_Player::mark(){
	Object::mark();
	gc_mark_q(m_footstep2_sound);
	gc_mark_q(m_huff_sound);
	gc_mark_q(m_sprite);
	gc_mark_q(m_sprite2);
}
String c_Player::debug(){
	String t="(Player)\n";
	t+=dbg_decl("sprite",&m_sprite);
	t+=dbg_decl("sprite2",&m_sprite2);
	t+=dbg_decl("x",&m_x);
	t+=dbg_decl("y",&m_y);
	t+=dbg_decl("Player_speed",&m_Player_speed);
	t+=dbg_decl("tick_sprint",&m_tick_sprint);
	t+=dbg_decl("sprint_energy",&m_sprint_energy);
	t+=dbg_decl("sprint_regeneration",&m_sprint_regeneration);
	t+=dbg_decl("travel",&m_travel);
	t+=dbg_decl("footstep2_sound",&m_footstep2_sound);
	t+=dbg_decl("huff_sound",&m_huff_sound);
	t+=dbg_decl("x_old",&m_x_old);
	t+=dbg_decl("y_old",&m_y_old);
	t+=dbg_decl("xspeed",&m_xspeed);
	t+=dbg_decl("yspeed",&m_yspeed);
	t+=dbg_decl("increm",&m_increm);
	t+=dbg_decl("sprint_multiplier",&m_sprint_multiplier);
	t+=dbg_decl("x_disp",&m_x_disp);
	t+=dbg_decl("y_disp",&m_y_disp);
	t+=dbg_decl("resultant_disp",&m_resultant_disp);
	t+=dbg_decl("up",&m_up);
	t+=dbg_decl("down",&m_down);
	t+=dbg_decl("left",&m_left);
	t+=dbg_decl("right",&m_right);
	return t;
}
c_RayGunBullet::c_RayGunBullet(){
	m_x=FLOAT(.0);
	m_y=FLOAT(.0);
	m_up=false;
	m_down=false;
	m_left=false;
	m_right=false;
	m_RayGunBullet_speed=10;
	m_zombies_hit=0;
	m_sprite=bb_graphics_LoadImage(String(L"raygun.png",10),1,c_Image::m_DefaultFlags);
}
c_RayGunBullet* c_RayGunBullet::m_new(Float t_x_spawn,Float t_y_spawn){
	DBG_ENTER("RayGunBullet.new")
	c_RayGunBullet *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_x_spawn,"x_spawn")
	DBG_LOCAL(t_y_spawn,"y_spawn")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<944>");
	m_x=t_x_spawn+FLOAT(10.0);
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<945>");
	m_y=t_y_spawn;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<946>");
	if((bb_input_KeyDown(87))!=0){
		DBG_BLOCK();
		m_up=true;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<947>");
	if((bb_input_KeyDown(83))!=0){
		DBG_BLOCK();
		m_down=true;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<948>");
	if((bb_input_KeyDown(65))!=0){
		DBG_BLOCK();
		m_left=true;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<949>");
	if((bb_input_KeyDown(68))!=0){
		DBG_BLOCK();
		m_right=true;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<951>");
	if(m_up==false && m_left==false && m_down==false && m_right==false){
		DBG_BLOCK();
		m_up=true;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<953>");
	if(m_left==true && m_right==true && m_down==true){
		DBG_BLOCK();
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<954>");
		m_right=false;
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<955>");
		m_left=false;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<957>");
	if(m_left==true && m_right==true && m_up==true){
		DBG_BLOCK();
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<958>");
		m_right=false;
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<959>");
		m_left=false;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<961>");
	if(m_up==true && m_down==true){
		DBG_BLOCK();
		m_down=false;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<962>");
	if(m_left==true && m_right==true){
		DBG_BLOCK();
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<963>");
		m_up=true;
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<964>");
		m_left=false;
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<965>");
		m_right=false;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<970>");
	if(m_up==true && m_left==true){
		DBG_BLOCK();
		m_RayGunBullet_speed=int(Float(m_RayGunBullet_speed)*FLOAT(0.70711));
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<972>");
	if(m_up==true && m_right==true){
		DBG_BLOCK();
		m_RayGunBullet_speed=int(Float(m_RayGunBullet_speed)*FLOAT(0.70711));
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<974>");
	if(m_down==true && m_left==true){
		DBG_BLOCK();
		m_RayGunBullet_speed=int(Float(m_RayGunBullet_speed)*FLOAT(0.70711));
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<976>");
	if(m_down==true && m_right==true){
		DBG_BLOCK();
		m_RayGunBullet_speed=int(Float(m_RayGunBullet_speed)*FLOAT(0.70711));
	}
	return this;
}
c_RayGunBullet* c_RayGunBullet::m_new2(){
	DBG_ENTER("RayGunBullet.new")
	c_RayGunBullet *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<929>");
	return this;
}
int c_RayGunBullet::p_Move2(String t_GameState){
	DBG_ENTER("RayGunBullet.Move")
	c_RayGunBullet *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_GameState,"GameState")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<987>");
	if(t_GameState==String(L"PLAYING",7) && m_up==true){
		DBG_BLOCK();
		m_y=m_y-Float(m_RayGunBullet_speed);
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<989>");
	if(t_GameState==String(L"PLAYING",7) && m_down==true){
		DBG_BLOCK();
		m_y=m_y+Float(m_RayGunBullet_speed);
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<991>");
	if(t_GameState==String(L"PLAYING",7) && m_left==true){
		DBG_BLOCK();
		m_x=m_x-Float(m_RayGunBullet_speed);
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<993>");
	if(t_GameState==String(L"PLAYING",7) && m_right==true){
		DBG_BLOCK();
		m_x=m_x+Float(m_RayGunBullet_speed);
	}
	return 0;
}
int c_RayGunBullet::p_health(int t_PaP_upgrades){
	DBG_ENTER("RayGunBullet.health")
	c_RayGunBullet *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_PaP_upgrades,"PaP_upgrades")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1003>");
	m_zombies_hit+=1;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1004>");
	if(m_zombies_hit>=2+t_PaP_upgrades){
		DBG_BLOCK();
		return 1;
	}
	return 0;
}
void c_RayGunBullet::mark(){
	Object::mark();
	gc_mark_q(m_sprite);
}
String c_RayGunBullet::debug(){
	String t="(RayGunBullet)\n";
	t+=dbg_decl("sprite",&m_sprite);
	t+=dbg_decl("x",&m_x);
	t+=dbg_decl("y",&m_y);
	t+=dbg_decl("left",&m_left);
	t+=dbg_decl("right",&m_right);
	t+=dbg_decl("up",&m_up);
	t+=dbg_decl("down",&m_down);
	t+=dbg_decl("RayGunBullet_speed",&m_RayGunBullet_speed);
	t+=dbg_decl("zombies_hit",&m_zombies_hit);
	return t;
}
c_List::c_List(){
	m__head=((new c_HeadNode)->m_new());
}
c_List* c_List::m_new(){
	DBG_ENTER("List.new")
	c_List *self=this;
	DBG_LOCAL(self,"Self")
	return this;
}
c_Node2* c_List::p_AddLast(c_RayGunBullet* t_data){
	DBG_ENTER("List.AddLast")
	c_List *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<108>");
	c_Node2* t_=(new c_Node2)->m_new(m__head,m__head->m__pred,t_data);
	return t_;
}
c_List* c_List::m_new2(Array<c_RayGunBullet* > t_data){
	DBG_ENTER("List.new")
	c_List *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<13>");
	Array<c_RayGunBullet* > t_=t_data;
	int t_2=0;
	while(t_2<t_.Length()){
		DBG_BLOCK();
		c_RayGunBullet* t_t=t_.At(t_2);
		t_2=t_2+1;
		DBG_LOCAL(t_t,"t")
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<14>");
		p_AddLast(t_t);
	}
	return this;
}
c_Enumerator3* c_List::p_ObjectEnumerator(){
	DBG_ENTER("List.ObjectEnumerator")
	c_List *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<186>");
	c_Enumerator3* t_=(new c_Enumerator3)->m_new(this);
	return t_;
}
bool c_List::p_Equals(c_RayGunBullet* t_lhs,c_RayGunBullet* t_rhs){
	DBG_ENTER("List.Equals")
	c_List *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_lhs,"lhs")
	DBG_LOCAL(t_rhs,"rhs")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<28>");
	bool t_=t_lhs==t_rhs;
	return t_;
}
int c_List::p_RemoveEach(c_RayGunBullet* t_value){
	DBG_ENTER("List.RemoveEach")
	c_List *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<151>");
	c_Node2* t_node=m__head->m__succ;
	DBG_LOCAL(t_node,"node")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<152>");
	while(t_node!=m__head){
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<153>");
		c_Node2* t_succ=t_node->m__succ;
		DBG_LOCAL(t_succ,"succ")
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<154>");
		if(p_Equals(t_node->m__data,t_value)){
			DBG_BLOCK();
			t_node->p_Remove2();
		}
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<155>");
		t_node=t_succ;
	}
	return 0;
}
void c_List::p_Remove(c_RayGunBullet* t_value){
	DBG_ENTER("List.Remove")
	c_List *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<137>");
	p_RemoveEach(t_value);
}
void c_List::mark(){
	Object::mark();
	gc_mark_q(m__head);
}
String c_List::debug(){
	String t="(List)\n";
	t+=dbg_decl("_head",&m__head);
	return t;
}
c_Node2::c_Node2(){
	m__succ=0;
	m__pred=0;
	m__data=0;
}
c_Node2* c_Node2::m_new(c_Node2* t_succ,c_Node2* t_pred,c_RayGunBullet* t_data){
	DBG_ENTER("Node.new")
	c_Node2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_succ,"succ")
	DBG_LOCAL(t_pred,"pred")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<261>");
	gc_assign(m__succ,t_succ);
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<262>");
	gc_assign(m__pred,t_pred);
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<263>");
	gc_assign(m__succ->m__pred,this);
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<264>");
	gc_assign(m__pred->m__succ,this);
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<265>");
	gc_assign(m__data,t_data);
	return this;
}
c_Node2* c_Node2::m_new2(){
	DBG_ENTER("Node.new")
	c_Node2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<258>");
	return this;
}
int c_Node2::p_Remove2(){
	DBG_ENTER("Node.Remove")
	c_Node2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<274>");
	if(m__succ->m__pred!=this){
		DBG_BLOCK();
		bbError(String(L"Illegal operation on removed node",33));
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<276>");
	gc_assign(m__succ->m__pred,m__pred);
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<277>");
	gc_assign(m__pred->m__succ,m__succ);
	return 0;
}
void c_Node2::mark(){
	Object::mark();
	gc_mark_q(m__succ);
	gc_mark_q(m__pred);
	gc_mark_q(m__data);
}
String c_Node2::debug(){
	String t="(Node)\n";
	t+=dbg_decl("_succ",&m__succ);
	t+=dbg_decl("_pred",&m__pred);
	t+=dbg_decl("_data",&m__data);
	return t;
}
c_HeadNode::c_HeadNode(){
}
c_HeadNode* c_HeadNode::m_new(){
	DBG_ENTER("HeadNode.new")
	c_HeadNode *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<310>");
	c_Node2::m_new2();
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<311>");
	gc_assign(m__succ,(this));
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<312>");
	gc_assign(m__pred,(this));
	return this;
}
void c_HeadNode::mark(){
	c_Node2::mark();
}
String c_HeadNode::debug(){
	String t="(HeadNode)\n";
	t=c_Node2::debug()+t;
	return t;
}
c_Zombie::c_Zombie(){
	m_x_Rnd=FLOAT(.0);
	m_y_Rnd=FLOAT(.0);
	m_x=FLOAT(.0);
	m_y=FLOAT(.0);
	m_Zombie_speed=FLOAT(0.85);
	m_angle=FLOAT(.0);
	m_player_advantage=false;
	m_tick_tap=0;
	m_damage_taken=0;
	m_sprite=bb_graphics_LoadImage(String(L"zombie.png",10),1,c_Image::m_DefaultFlags);
}
c_Zombie* c_Zombie::m_new(){
	DBG_ENTER("Zombie.new")
	c_Zombie *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<880>");
	m_x_Rnd=bb_random_Rnd2(FLOAT(0.0),FLOAT(1.0));
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<881>");
	m_y_Rnd=bb_random_Rnd2(FLOAT(0.0),FLOAT(1.0));
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<883>");
	if(m_x_Rnd<FLOAT(0.5)){
		DBG_BLOCK();
		m_x=bb_random_Rnd2(FLOAT(-600.0),FLOAT(-300.0));
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<884>");
	if(m_x_Rnd>=FLOAT(0.5)){
		DBG_BLOCK();
		m_x=bb_random_Rnd2(FLOAT(850.0),FLOAT(1200.0));
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<886>");
	if(m_y_Rnd<FLOAT(0.5)){
		DBG_BLOCK();
		m_y=bb_random_Rnd2(FLOAT(-700.0),FLOAT(-400.0));
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<887>");
	if(m_y_Rnd>=FLOAT(0.5)){
		DBG_BLOCK();
		m_y=bb_random_Rnd2(FLOAT(700.0),FLOAT(1200.0));
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<889>");
	if(bb_random_Rnd2(FLOAT(0.0),FLOAT(1.0))>FLOAT(0.9)){
		DBG_BLOCK();
		m_Zombie_speed=FLOAT(2.0);
	}
	return this;
}
int c_Zombie::p_Move3(Float t_playerx,Float t_playery,String t_GameState,bool t_enemy_collision,bool t_player_collision,int t_tick){
	DBG_ENTER("Zombie.Move")
	c_Zombie *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_playerx,"playerx")
	DBG_LOCAL(t_playery,"playery")
	DBG_LOCAL(t_GameState,"GameState")
	DBG_LOCAL(t_enemy_collision,"enemy_collision")
	DBG_LOCAL(t_player_collision,"player_collision")
	DBG_LOCAL(t_tick,"tick")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<895>");
	m_angle=(Float)(atan2(t_playery-FLOAT(5.0)-m_y,t_playerx-FLOAT(2.0)-m_x)*R2D);
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<897>");
	if(t_enemy_collision==true){
		DBG_BLOCK();
		m_Zombie_speed=FLOAT(0.98);
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<898>");
	if(t_GameState==String(L"PLAYING",7) && m_player_advantage==false){
		DBG_BLOCK();
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<899>");
		m_x+=m_Zombie_speed*(Float)cos((m_angle)*D2R);
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<900>");
		m_y+=m_Zombie_speed*(Float)sin((m_angle)*D2R);
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<903>");
	if(m_tick_tap==t_tick && m_player_advantage==true){
		DBG_BLOCK();
		m_player_advantage=false;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<905>");
	if(t_player_collision==true){
		DBG_BLOCK();
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<906>");
		if(t_tick>-1){
			DBG_BLOCK();
			m_tick_tap=int(bb_random_Rnd2(FLOAT(30.0),FLOAT(60.0)));
		}
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<907>");
		if(t_tick>30){
			DBG_BLOCK();
			m_tick_tap=int(bb_random_Rnd2(FLOAT(60.0),FLOAT(90.0)));
		}
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<908>");
		if(t_tick>60){
			DBG_BLOCK();
			m_tick_tap=int(bb_random_Rnd2(FLOAT(90.0),FLOAT(120.0)));
		}
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<909>");
		if(t_tick>90){
			DBG_BLOCK();
			m_tick_tap=int(bb_random_Rnd2(FLOAT(0.0),FLOAT(30.0)));
		}
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<910>");
		m_player_advantage=true;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<913>");
	if(t_enemy_collision==false){
		DBG_BLOCK();
		m_Zombie_speed=FLOAT(1.1);
	}
	return 0;
}
int c_Zombie::p_dead(int t_health_zombie,int t_damage_inflicted){
	DBG_ENTER("Zombie.dead")
	c_Zombie *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_health_zombie,"health_zombie")
	DBG_LOCAL(t_damage_inflicted,"damage_inflicted")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<920>");
	m_damage_taken+=t_damage_inflicted;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<921>");
	if(m_damage_taken>=t_health_zombie){
		DBG_BLOCK();
		return 1;
	}
	return 0;
}
void c_Zombie::mark(){
	Object::mark();
	gc_mark_q(m_sprite);
}
String c_Zombie::debug(){
	String t="(Zombie)\n";
	t+=dbg_decl("sprite",&m_sprite);
	t+=dbg_decl("x",&m_x);
	t+=dbg_decl("y",&m_y);
	t+=dbg_decl("Zombie_speed",&m_Zombie_speed);
	t+=dbg_decl("x_Rnd",&m_x_Rnd);
	t+=dbg_decl("y_Rnd",&m_y_Rnd);
	t+=dbg_decl("damage_taken",&m_damage_taken);
	t+=dbg_decl("angle",&m_angle);
	t+=dbg_decl("tick_tap",&m_tick_tap);
	t+=dbg_decl("player_advantage",&m_player_advantage);
	return t;
}
c_List2::c_List2(){
	m__head=((new c_HeadNode2)->m_new());
}
c_List2* c_List2::m_new(){
	DBG_ENTER("List.new")
	c_List2 *self=this;
	DBG_LOCAL(self,"Self")
	return this;
}
c_Node3* c_List2::p_AddLast2(c_Zombie* t_data){
	DBG_ENTER("List.AddLast")
	c_List2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<108>");
	c_Node3* t_=(new c_Node3)->m_new(m__head,m__head->m__pred,t_data);
	return t_;
}
c_List2* c_List2::m_new2(Array<c_Zombie* > t_data){
	DBG_ENTER("List.new")
	c_List2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<13>");
	Array<c_Zombie* > t_=t_data;
	int t_2=0;
	while(t_2<t_.Length()){
		DBG_BLOCK();
		c_Zombie* t_t=t_.At(t_2);
		t_2=t_2+1;
		DBG_LOCAL(t_t,"t")
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<14>");
		p_AddLast2(t_t);
	}
	return this;
}
c_Enumerator4* c_List2::p_ObjectEnumerator(){
	DBG_ENTER("List.ObjectEnumerator")
	c_List2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<186>");
	c_Enumerator4* t_=(new c_Enumerator4)->m_new(this);
	return t_;
}
bool c_List2::p_Equals2(c_Zombie* t_lhs,c_Zombie* t_rhs){
	DBG_ENTER("List.Equals")
	c_List2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_lhs,"lhs")
	DBG_LOCAL(t_rhs,"rhs")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<28>");
	bool t_=t_lhs==t_rhs;
	return t_;
}
int c_List2::p_RemoveEach2(c_Zombie* t_value){
	DBG_ENTER("List.RemoveEach")
	c_List2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<151>");
	c_Node3* t_node=m__head->m__succ;
	DBG_LOCAL(t_node,"node")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<152>");
	while(t_node!=m__head){
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<153>");
		c_Node3* t_succ=t_node->m__succ;
		DBG_LOCAL(t_succ,"succ")
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<154>");
		if(p_Equals2(t_node->m__data,t_value)){
			DBG_BLOCK();
			t_node->p_Remove2();
		}
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<155>");
		t_node=t_succ;
	}
	return 0;
}
void c_List2::p_Remove3(c_Zombie* t_value){
	DBG_ENTER("List.Remove")
	c_List2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<137>");
	p_RemoveEach2(t_value);
}
void c_List2::mark(){
	Object::mark();
	gc_mark_q(m__head);
}
String c_List2::debug(){
	String t="(List)\n";
	t+=dbg_decl("_head",&m__head);
	return t;
}
c_Node3::c_Node3(){
	m__succ=0;
	m__pred=0;
	m__data=0;
}
c_Node3* c_Node3::m_new(c_Node3* t_succ,c_Node3* t_pred,c_Zombie* t_data){
	DBG_ENTER("Node.new")
	c_Node3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_succ,"succ")
	DBG_LOCAL(t_pred,"pred")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<261>");
	gc_assign(m__succ,t_succ);
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<262>");
	gc_assign(m__pred,t_pred);
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<263>");
	gc_assign(m__succ->m__pred,this);
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<264>");
	gc_assign(m__pred->m__succ,this);
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<265>");
	gc_assign(m__data,t_data);
	return this;
}
c_Node3* c_Node3::m_new2(){
	DBG_ENTER("Node.new")
	c_Node3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<258>");
	return this;
}
int c_Node3::p_Remove2(){
	DBG_ENTER("Node.Remove")
	c_Node3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<274>");
	if(m__succ->m__pred!=this){
		DBG_BLOCK();
		bbError(String(L"Illegal operation on removed node",33));
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<276>");
	gc_assign(m__succ->m__pred,m__pred);
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<277>");
	gc_assign(m__pred->m__succ,m__succ);
	return 0;
}
void c_Node3::mark(){
	Object::mark();
	gc_mark_q(m__succ);
	gc_mark_q(m__pred);
	gc_mark_q(m__data);
}
String c_Node3::debug(){
	String t="(Node)\n";
	t+=dbg_decl("_succ",&m__succ);
	t+=dbg_decl("_pred",&m__pred);
	t+=dbg_decl("_data",&m__data);
	return t;
}
c_HeadNode2::c_HeadNode2(){
}
c_HeadNode2* c_HeadNode2::m_new(){
	DBG_ENTER("HeadNode.new")
	c_HeadNode2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<310>");
	c_Node3::m_new2();
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<311>");
	gc_assign(m__succ,(this));
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<312>");
	gc_assign(m__pred,(this));
	return this;
}
void c_HeadNode2::mark(){
	c_Node3::mark();
}
String c_HeadNode2::debug(){
	String t="(HeadNode)\n";
	t=c_Node3::debug()+t;
	return t;
}
int bb_random_Seed;
Float bb_random_Rnd(){
	DBG_ENTER("Rnd")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/random.monkey<21>");
	bb_random_Seed=bb_random_Seed*1664525+1013904223|0;
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/random.monkey<22>");
	Float t_=Float(bb_random_Seed>>8&16777215)/FLOAT(16777216.0);
	return t_;
}
Float bb_random_Rnd2(Float t_low,Float t_high){
	DBG_ENTER("Rnd")
	DBG_LOCAL(t_low,"low")
	DBG_LOCAL(t_high,"high")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/random.monkey<30>");
	Float t_=bb_random_Rnd3(t_high-t_low)+t_low;
	return t_;
}
Float bb_random_Rnd3(Float t_range){
	DBG_ENTER("Rnd")
	DBG_LOCAL(t_range,"range")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/random.monkey<26>");
	Float t_=bb_random_Rnd()*t_range;
	return t_;
}
int bb_app__updateRate;
void bb_app_SetUpdateRate(int t_hertz){
	DBG_ENTER("SetUpdateRate")
	DBG_LOCAL(t_hertz,"hertz")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<224>");
	bb_app__updateRate=t_hertz;
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/app.monkey<225>");
	bb_app__game->SetUpdateRate(t_hertz);
}
c_Sound::c_Sound(){
	m_sample=0;
}
c_Sound* c_Sound::m_new(gxtkSample* t_sample){
	DBG_ENTER("Sound.new")
	c_Sound *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_sample,"sample")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/audio.monkey<32>");
	gc_assign(this->m_sample,t_sample);
	return this;
}
c_Sound* c_Sound::m_new2(){
	DBG_ENTER("Sound.new")
	c_Sound *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/audio.monkey<29>");
	return this;
}
void c_Sound::mark(){
	Object::mark();
	gc_mark_q(m_sample);
}
String c_Sound::debug(){
	String t="(Sound)\n";
	t+=dbg_decl("sample",&m_sample);
	return t;
}
c_Sound* bb_audio_LoadSound(String t_path){
	DBG_ENTER("LoadSound")
	DBG_LOCAL(t_path,"path")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/audio.monkey<47>");
	gxtkSample* t_sample=bb_audio_device->LoadSample(bb_data_FixDataPath(t_path));
	DBG_LOCAL(t_sample,"sample")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/audio.monkey<48>");
	if((t_sample)!=0){
		DBG_BLOCK();
		c_Sound* t_=(new c_Sound)->m_new(t_sample);
		return t_;
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/audio.monkey<49>");
	return 0;
}
c_TFont::c_TFont(){
	m_Stream=0;
	m_Size=0;
	m_Color=Array<int >();
	m_Path=String();
	m_OutlineType=String();
	m_GlyphNumber=0;
	m_FontLimits=Array<int >(4);
	m_FontScale=FLOAT(.0);
	m_LineHeight=0;
	m_GlyphId=Array<int >();
	m_Glyph=Array<c_TFont_Glyph* >();
	m_ClockwiseFound=false;
	m_FontClockwise=false;
	m_ImagesLoaded=false;
}
int c_TFont::p_LoadMetrics(int t_Offset,int t_HMetricsCount){
	DBG_ENTER("TFont.LoadMetrics")
	c_TFont *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_Offset,"Offset")
	DBG_LOCAL(t_HMetricsCount,"HMetricsCount")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<190>");
	m_Stream->p_SetPointer(t_Offset);
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<191>");
	int t_Count=0;
	int t_LastAdv=0;
	DBG_LOCAL(t_Count,"Count")
	DBG_LOCAL(t_LastAdv,"LastAdv")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<192>");
	for(int t_i=0;t_i<=m_GlyphNumber-1;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<193>");
		if(t_Count<t_HMetricsCount-1){
			DBG_BLOCK();
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<194>");
			m_Glyph.At(t_i)->m_Adv=m_Stream->p_ReadUInt(2);
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<195>");
			m_Glyph.At(t_i)->m_Lsb=m_Stream->p_ReadInt(2);
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<196>");
			t_LastAdv=m_Glyph.At(t_i)->m_Adv;
		}else{
			DBG_BLOCK();
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<198>");
			m_Glyph.At(t_i)->m_Adv=t_LastAdv;
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<199>");
			m_Glyph.At(t_i)->m_Lsb=m_Stream->p_ReadInt(2);
		}
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<201>");
		t_Count=t_Count+1;
	}
	return 0;
}
int c_TFont::p_LoadCmapTable0(int t_Offset){
	DBG_ENTER("TFont.LoadCmapTable0")
	c_TFont *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_Offset,"Offset")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<257>");
	m_Stream->p_SetPointer(t_Offset);
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<258>");
	m_Stream->p_ReadUInt(2);
	m_Stream->p_ReadUInt(2);
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<259>");
	for(int t_g=0;t_g<=254;t_g=t_g+1){
		DBG_BLOCK();
		DBG_LOCAL(t_g,"g")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<260>");
		int t_GId=m_Stream->p_ReadUInt(1);
		DBG_LOCAL(t_GId,"GId")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<261>");
		if(m_GlyphId.At(t_g)==0){
			DBG_BLOCK();
			m_GlyphId.At(t_g)=t_GId;
		}
	}
	return 0;
}
int c_TFont::p_LoadCmapTable4(int t_Offset){
	DBG_ENTER("TFont.LoadCmapTable4")
	c_TFont *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_Offset,"Offset")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<265>");
	int t_OffCount=t_Offset;
	DBG_LOCAL(t_OffCount,"OffCount")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<266>");
	m_Stream->p_SetPointer(t_Offset);
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<267>");
	m_Stream->p_ReadUInt(2);
	m_Stream->p_ReadUInt(2);
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<268>");
	int t_SegCount=m_Stream->p_ReadUInt(2)/2;
	DBG_LOCAL(t_SegCount,"SegCount")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<269>");
	int t_SearchRange=m_Stream->p_ReadUInt(2);
	DBG_LOCAL(t_SearchRange,"SearchRange")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<270>");
	int t_EntrySelector=m_Stream->p_ReadUInt(2);
	DBG_LOCAL(t_EntrySelector,"EntrySelector")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<271>");
	int t_RangeShift=m_Stream->p_ReadUInt(2);
	DBG_LOCAL(t_RangeShift,"RangeShift")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<272>");
	t_OffCount=t_OffCount+12;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<273>");
	Array<int > t_EndCount=Array<int >(t_SegCount);
	DBG_LOCAL(t_EndCount,"EndCount")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<274>");
	for(int t_s=0;t_s<=t_SegCount-1;t_s=t_s+1){
		DBG_BLOCK();
		DBG_LOCAL(t_s,"s")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<275>");
		t_EndCount.At(t_s)=m_Stream->p_ReadUInt(2);
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<276>");
		t_OffCount=t_OffCount+2;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<278>");
	int t_Reserved=m_Stream->p_ReadUInt(2);
	t_OffCount=t_OffCount+2;
	DBG_LOCAL(t_Reserved,"Reserved")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<279>");
	Array<int > t_StartCount=Array<int >(t_SegCount);
	DBG_LOCAL(t_StartCount,"StartCount")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<280>");
	for(int t_s2=0;t_s2<=t_SegCount-1;t_s2=t_s2+1){
		DBG_BLOCK();
		DBG_LOCAL(t_s2,"s")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<281>");
		t_StartCount.At(t_s2)=m_Stream->p_ReadUInt(2);
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<282>");
		t_OffCount=t_OffCount+2;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<284>");
	Array<int > t_IdDelta=Array<int >(t_SegCount);
	DBG_LOCAL(t_IdDelta,"IdDelta")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<285>");
	for(int t_s3=0;t_s3<=t_SegCount-1;t_s3=t_s3+1){
		DBG_BLOCK();
		DBG_LOCAL(t_s3,"s")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<286>");
		t_IdDelta.At(t_s3)=m_Stream->p_ReadInt(2);
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<287>");
		t_OffCount=t_OffCount+2;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<289>");
	Array<int > t_IdRangeOffset=Array<int >(t_SegCount);
	DBG_LOCAL(t_IdRangeOffset,"IdRangeOffset")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<290>");
	Array<int > t_IdRangeOffsetOffset=Array<int >(t_SegCount);
	DBG_LOCAL(t_IdRangeOffsetOffset,"IdRangeOffsetOffset")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<291>");
	for(int t_s4=0;t_s4<=t_SegCount-1;t_s4=t_s4+1){
		DBG_BLOCK();
		DBG_LOCAL(t_s4,"s")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<292>");
		t_IdRangeOffset.At(t_s4)=m_Stream->p_ReadUInt(2);
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<293>");
		t_IdRangeOffsetOffset.At(t_s4)=t_OffCount;
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<294>");
		t_OffCount=t_OffCount+2;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<297>");
	for(int t_Char=0;t_Char<=m_GlyphNumber-1;t_Char=t_Char+1){
		DBG_BLOCK();
		DBG_LOCAL(t_Char,"Char")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<298>");
		int t_NullFlag=1;
		DBG_LOCAL(t_NullFlag,"NullFlag")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<300>");
		int t_CharSeg=0;
		DBG_LOCAL(t_CharSeg,"CharSeg")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<301>");
		for(int t_s5=0;t_s5<=t_SegCount-1;t_s5=t_s5+1){
			DBG_BLOCK();
			DBG_LOCAL(t_s5,"s")
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<302>");
			if(t_EndCount.At(t_s5)>=t_Char){
				DBG_BLOCK();
				DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<303>");
				t_CharSeg=t_s5;
				DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<304>");
				if(t_Char>=t_StartCount.At(t_s5)){
					DBG_BLOCK();
					t_NullFlag=0;
				}
				DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<305>");
				break;
			}
		}
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<308>");
		if(t_NullFlag==1){
			DBG_BLOCK();
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<309>");
			m_GlyphId.At(t_Char)=0;
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<310>");
			continue;
		}
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<313>");
		if(t_IdRangeOffset.At(t_CharSeg)==0){
			DBG_BLOCK();
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<314>");
			if(m_GlyphId.At(t_Char)==0){
				DBG_BLOCK();
				m_GlyphId.At(t_Char)=t_IdDelta.At(t_CharSeg)+t_Char;
			}
		}else{
			DBG_BLOCK();
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<316>");
			int t_Location=2*(t_Char-t_StartCount.At(t_CharSeg))+(t_IdRangeOffset.At(t_CharSeg)-t_IdRangeOffset.At(0))+t_OffCount+t_CharSeg*2;
			DBG_LOCAL(t_Location,"Location")
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<317>");
			if(m_GlyphId.At(t_Char)==0){
				DBG_BLOCK();
				m_GlyphId.At(t_Char)=m_Stream->p_PeekUInt(2,t_Location);
			}
		}
	}
	return 0;
}
int c_TFont::p_LoadCmapTable6(int t_Offset){
	DBG_ENTER("TFont.LoadCmapTable6")
	c_TFont *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_Offset,"Offset")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<322>");
	m_Stream->p_SetPointer(t_Offset);
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<323>");
	m_Stream->p_ReadUInt(2);
	m_Stream->p_ReadUInt(2);
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<324>");
	int t_FirstCode=m_Stream->p_ReadUInt(2);
	DBG_LOCAL(t_FirstCode,"FirstCode")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<325>");
	int t_EntryCount=m_Stream->p_ReadUInt(2);
	DBG_LOCAL(t_EntryCount,"EntryCount")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<326>");
	for(int t_g=t_FirstCode;t_g<=t_EntryCount-1;t_g=t_g+1){
		DBG_BLOCK();
		DBG_LOCAL(t_g,"g")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<327>");
		int t_GId=m_Stream->p_ReadUInt(2);
		DBG_LOCAL(t_GId,"GId")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<328>");
		if(m_GlyphId.At(t_g)==0){
			DBG_BLOCK();
			m_GlyphId.At(t_g)=t_GId;
		}
	}
	return 0;
}
int c_TFont::p_LoadCmapData(int t_Offset){
	DBG_ENTER("TFont.LoadCmapData")
	c_TFont *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_Offset,"Offset")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<205>");
	m_Stream->p_SetPointer(t_Offset);
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<206>");
	m_Stream->p_ReadUInt(2);
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<207>");
	int t_numTables=m_Stream->p_ReadUInt(2);
	DBG_LOCAL(t_numTables,"numTables")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<208>");
	Array<int > t_PlatformId=Array<int >(t_numTables);
	DBG_LOCAL(t_PlatformId,"PlatformId")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<209>");
	Array<int > t_EncodingId=Array<int >(t_numTables);
	DBG_LOCAL(t_EncodingId,"EncodingId")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<210>");
	Array<int > t_TableOffset=Array<int >(t_numTables);
	DBG_LOCAL(t_TableOffset,"TableOffset")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<213>");
	for(int t_t=0;t_t<=t_numTables-1;t_t=t_t+1){
		DBG_BLOCK();
		DBG_LOCAL(t_t,"t")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<214>");
		t_PlatformId.At(t_t)=m_Stream->p_ReadUInt(2);
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<215>");
		t_EncodingId.At(t_t)=m_Stream->p_ReadUInt(2);
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<216>");
		t_TableOffset.At(t_t)=m_Stream->p_ReadUInt(4)+t_Offset;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<220>");
	bool t_WindowsFontFound=false;
	DBG_LOCAL(t_WindowsFontFound,"WindowsFontFound")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<221>");
	for(int t_t2=0;t_t2<=t_numTables-1;t_t2=t_t2+1){
		DBG_BLOCK();
		DBG_LOCAL(t_t2,"t")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<222>");
		if(t_PlatformId.At(t_t2)==3 && t_EncodingId.At(t_t2)==1){
			DBG_BLOCK();
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<223>");
			t_WindowsFontFound=true;
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<224>");
			int t_Format=m_Stream->p_PeekUInt(2,t_TableOffset.At(t_t2));
			DBG_LOCAL(t_Format,"Format")
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<225>");
			if(t_Format==0){
				DBG_BLOCK();
				p_LoadCmapTable0(t_TableOffset.At(t_t2)+2);
			}
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<226>");
			if(t_Format==4){
				DBG_BLOCK();
				p_LoadCmapTable4(t_TableOffset.At(t_t2)+2);
			}
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<227>");
			if(t_Format==6){
				DBG_BLOCK();
				p_LoadCmapTable6(t_TableOffset.At(t_t2)+2);
			}
		}
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<232>");
	for(int t_t3=0;t_t3<=t_numTables-1;t_t3=t_t3+1){
		DBG_BLOCK();
		DBG_LOCAL(t_t3,"t")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<233>");
		if(t_PlatformId.At(t_t3)==3 && t_EncodingId.At(t_t3)!=1){
			DBG_BLOCK();
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<234>");
			int t_Format2=m_Stream->p_PeekUInt(2,t_TableOffset.At(t_t3));
			DBG_LOCAL(t_Format2,"Format")
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<235>");
			if(t_Format2==0){
				DBG_BLOCK();
				p_LoadCmapTable0(t_TableOffset.At(t_t3)+2);
			}
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<236>");
			if(t_Format2==4){
				DBG_BLOCK();
				p_LoadCmapTable4(t_TableOffset.At(t_t3)+2);
			}
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<237>");
			if(t_Format2==6){
				DBG_BLOCK();
				p_LoadCmapTable6(t_TableOffset.At(t_t3)+2);
			}
		}
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<242>");
	for(int t_t4=0;t_t4<=t_numTables-1;t_t4=t_t4+1){
		DBG_BLOCK();
		DBG_LOCAL(t_t4,"t")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<243>");
		if(t_PlatformId.At(t_t4)!=3){
			DBG_BLOCK();
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<244>");
			int t_Format3=m_Stream->p_PeekUInt(2,t_TableOffset.At(t_t4));
			DBG_LOCAL(t_Format3,"Format")
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<245>");
			if(t_Format3==0){
				DBG_BLOCK();
				p_LoadCmapTable0(t_TableOffset.At(t_t4)+2);
			}
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<246>");
			if(t_Format3==4){
				DBG_BLOCK();
				p_LoadCmapTable4(t_TableOffset.At(t_t4)+2);
			}
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<247>");
			if(t_Format3==6){
				DBG_BLOCK();
				p_LoadCmapTable6(t_TableOffset.At(t_t4)+2);
			}
		}
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<252>");
	for(int t_i=0;t_i<=m_GlyphId.Length()-1;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<253>");
		if(m_GlyphId.At(t_i)>m_GlyphNumber-1){
			DBG_BLOCK();
			m_GlyphId.At(t_i)=0;
		}
	}
	return 0;
}
int c_TFont::p_LoadLoca(int t_Offset,int t_Format,int t_GlyfOffset){
	DBG_ENTER("TFont.LoadLoca")
	c_TFont *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_Offset,"Offset")
	DBG_LOCAL(t_Format,"Format")
	DBG_LOCAL(t_GlyfOffset,"GlyfOffset")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<332>");
	m_Stream->p_SetPointer(t_Offset);
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<333>");
	for(int t_i=0;t_i<=m_GlyphNumber-1;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<334>");
		if(t_Format==0){
			DBG_BLOCK();
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<335>");
			m_Glyph.At(t_i)->m_FileAddress=m_Stream->p_ReadUInt(2)*2+t_GlyfOffset;
		}else{
			DBG_BLOCK();
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<337>");
			m_Glyph.At(t_i)->m_FileAddress=m_Stream->p_ReadUInt(4)+t_GlyfOffset;
		}
	}
	return 0;
}
int c_TFont::p_LoadGlyfData(int t_Id,int t_Offset){
	DBG_ENTER("TFont.LoadGlyfData")
	c_TFont *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_Id,"Id")
	DBG_LOCAL(t_Offset,"Offset")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<342>");
	m_Stream->p_SetPointer(t_Offset);
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<344>");
	int t_ContourNumber=m_Stream->p_ReadInt(2);
	DBG_LOCAL(t_ContourNumber,"ContourNumber")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<345>");
	if(t_ContourNumber<1){
		DBG_BLOCK();
		return 0;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<346>");
	m_Glyph.At(t_Id)->m_ContourNumber=t_ContourNumber;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<347>");
	m_Glyph.At(t_Id)->m_xMin=m_Stream->p_ReadInt(2);
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<348>");
	m_Glyph.At(t_Id)->m_yMin=m_Stream->p_ReadInt(2);
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<349>");
	m_Glyph.At(t_Id)->m_xMax=m_Stream->p_ReadInt(2);
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<350>");
	m_Glyph.At(t_Id)->m_yMax=m_Stream->p_ReadInt(2);
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<351>");
	m_Glyph.At(t_Id)->m_W=m_Glyph.At(t_Id)->m_xMax-m_Glyph.At(t_Id)->m_xMin;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<352>");
	m_Glyph.At(t_Id)->m_H=m_Glyph.At(t_Id)->m_yMax-m_Glyph.At(t_Id)->m_yMin;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<358>");
	Array<int > t_EndPoints=Array<int >(t_ContourNumber);
	DBG_LOCAL(t_EndPoints,"EndPoints")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<359>");
	for(int t_i=0;t_i<=t_ContourNumber-1;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<360>");
		t_EndPoints.At(t_i)=m_Stream->p_ReadUInt(2);
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<362>");
	int t_PointNumber=t_EndPoints.At(t_ContourNumber-1)+1;
	DBG_LOCAL(t_PointNumber,"PointNumber")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<365>");
	int t_insLen=m_Stream->p_ReadUInt(2);
	DBG_LOCAL(t_insLen,"insLen")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<366>");
	m_Stream->p_ReadString(t_insLen);
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<369>");
	Array<Array<int > > t_Flags=Array<Array<int > >(t_PointNumber);
	DBG_LOCAL(t_Flags,"Flags")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<370>");
	int t_ContinueNumber=0;
	DBG_LOCAL(t_ContinueNumber,"ContinueNumber")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<371>");
	for(int t_i2=0;t_i2<=t_PointNumber-1;t_i2=t_i2+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i2,"i")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<373>");
		if(t_ContinueNumber>0){
			DBG_BLOCK();
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<374>");
			gc_assign(t_Flags.At(t_i2),t_Flags.At(t_i2-1));
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<375>");
			t_ContinueNumber=t_ContinueNumber-1;
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<376>");
			continue;
		}
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<379>");
		gc_assign(t_Flags.At(t_i2),m_Stream->p_ReadBits(1));
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<380>");
		if(t_Flags.At(t_i2).At(3)==1){
			DBG_BLOCK();
			t_ContinueNumber=m_Stream->p_ReadUInt(1);
		}
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<385>");
	Array<int > t_XCoords=Array<int >(t_PointNumber);
	DBG_LOCAL(t_XCoords,"XCoords")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<386>");
	for(int t_i3=0;t_i3<=t_PointNumber-1;t_i3=t_i3+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i3,"i")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<388>");
		if(t_Flags.At(t_i3).At(1)==0 && t_Flags.At(t_i3).At(4)==1){
			DBG_BLOCK();
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<389>");
			if(t_i3>0){
				DBG_BLOCK();
				t_XCoords.At(t_i3)=t_XCoords.At(t_i3-1);
			}else{
				DBG_BLOCK();
				t_XCoords.At(t_i3)=-m_Glyph.At(t_Id)->m_xMin;
			}
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<390>");
			continue;
		}
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<393>");
		if(t_Flags.At(t_i3).At(1)==1){
			DBG_BLOCK();
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<394>");
			int t_tmp=m_Stream->p_ReadUInt(1);
			DBG_LOCAL(t_tmp,"tmp")
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<395>");
			if(t_Flags.At(t_i3).At(4)==0){
				DBG_BLOCK();
				t_tmp=t_tmp*-1;
			}
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<396>");
			if(t_i3>0){
				DBG_BLOCK();
				t_XCoords.At(t_i3)=t_XCoords.At(t_i3-1)+t_tmp;
			}else{
				DBG_BLOCK();
				t_XCoords.At(t_i3)=t_tmp-m_Glyph.At(t_Id)->m_xMin;
			}
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<397>");
			continue;
		}
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<399>");
		if(t_Flags.At(t_i3).At(1)==0 && t_Flags.At(t_i3).At(4)==0){
			DBG_BLOCK();
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<400>");
			if(t_i3>0){
				DBG_BLOCK();
				t_XCoords.At(t_i3)=t_XCoords.At(t_i3-1)+m_Stream->p_ReadInt(2);
			}else{
				DBG_BLOCK();
				t_XCoords.At(t_i3)=m_Stream->p_ReadInt(2)-m_Glyph.At(t_Id)->m_xMin;
			}
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<401>");
			continue;
		}
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<409>");
	Array<int > t_YCoords=Array<int >(t_PointNumber);
	DBG_LOCAL(t_YCoords,"YCoords")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<410>");
	for(int t_i4=0;t_i4<=t_PointNumber-1;t_i4=t_i4+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i4,"i")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<412>");
		if(t_Flags.At(t_i4).At(2)==0 && t_Flags.At(t_i4).At(5)==1){
			DBG_BLOCK();
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<413>");
			if(t_i4>0){
				DBG_BLOCK();
				t_YCoords.At(t_i4)=t_YCoords.At(t_i4-1);
			}else{
				DBG_BLOCK();
				t_YCoords.At(t_i4)=m_Glyph.At(t_Id)->m_yMax;
			}
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<414>");
			continue;
		}
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<417>");
		if(t_Flags.At(t_i4).At(2)==1){
			DBG_BLOCK();
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<418>");
			int t_tmp2=m_Stream->p_ReadUInt(1);
			DBG_LOCAL(t_tmp2,"tmp")
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<419>");
			if(t_Flags.At(t_i4).At(5)==0){
				DBG_BLOCK();
				t_tmp2=t_tmp2*-1;
			}
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<420>");
			if(t_i4>0){
				DBG_BLOCK();
				t_YCoords.At(t_i4)=t_YCoords.At(t_i4-1)-t_tmp2;
			}else{
				DBG_BLOCK();
				t_YCoords.At(t_i4)=m_Glyph.At(t_Id)->m_yMax-t_tmp2;
			}
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<421>");
			continue;
		}
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<423>");
		if(t_Flags.At(t_i4).At(2)==0 && t_Flags.At(t_i4).At(5)==0){
			DBG_BLOCK();
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<424>");
			if(t_i4>0){
				DBG_BLOCK();
				t_YCoords.At(t_i4)=t_YCoords.At(t_i4-1)-m_Stream->p_ReadInt(2);
			}else{
				DBG_BLOCK();
				t_YCoords.At(t_i4)=m_Glyph.At(t_Id)->m_yMax-m_Stream->p_ReadInt(2);
			}
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<425>");
			continue;
		}
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<430>");
	gc_assign(m_Glyph.At(t_Id)->m_xyList,Array<Array<Float > >(t_ContourNumber));
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<431>");
	int t_p1=0;
	int t_Pend=0;
	DBG_LOCAL(t_p1,"p1")
	DBG_LOCAL(t_Pend,"Pend")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<432>");
	for(int t_i5=0;t_i5<=t_ContourNumber-1;t_i5=t_i5+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i5,"i")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<433>");
		if(t_i5>0){
			DBG_BLOCK();
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<434>");
			t_p1=t_EndPoints.At(t_i5-1)+1;
		}
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<436>");
		t_Pend=t_EndPoints.At(t_i5);
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<437>");
		gc_assign(m_Glyph.At(t_Id)->m_xyList.At(t_i5),Array<Float >((t_Pend-t_p1+1)*3));
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<438>");
		int t_Count=0;
		DBG_LOCAL(t_Count,"Count")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<439>");
		for(int t_j=t_p1;t_j<=t_Pend;t_j=t_j+1){
			DBG_BLOCK();
			DBG_LOCAL(t_j,"j")
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<440>");
			m_Glyph.At(t_Id)->m_xyList.At(t_i5).At(t_Count)=Float(t_XCoords.At(t_j));
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<441>");
			m_Glyph.At(t_Id)->m_xyList.At(t_i5).At(t_Count+1)=Float(t_YCoords.At(t_j));
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<442>");
			m_Glyph.At(t_Id)->m_xyList.At(t_i5).At(t_Count+2)=Float(t_Flags.At(t_j).At(0));
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<443>");
			t_Count=t_Count+3;
		}
	}
	return 0;
}
int c_TFont::p_QuadGlyph(int t_Id){
	DBG_ENTER("TFont.QuadGlyph")
	c_TFont *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_Id,"Id")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<449>");
	for(int t_c=0;t_c<=m_Glyph.At(t_Id)->m_ContourNumber-1;t_c=t_c+1){
		DBG_BLOCK();
		DBG_LOCAL(t_c,"c")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<450>");
		c_Stack2* t_xyStack=(new c_Stack2)->m_new();
		DBG_LOCAL(t_xyStack,"xyStack")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<451>");
		for(int t_p0=0;t_p0<=m_Glyph.At(t_Id)->m_xyList.At(t_c).Length()-1;t_p0=t_p0+3){
			DBG_BLOCK();
			DBG_LOCAL(t_p0,"p0")
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<452>");
			int t_p1=t_p0+3;
			if(t_p1>m_Glyph.At(t_Id)->m_xyList.At(t_c).Length()-1){
				DBG_BLOCK();
				t_p1=0;
			}
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<454>");
			t_xyStack->p_Push4(m_Glyph.At(t_Id)->m_xyList.At(t_c).At(t_p0));
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<455>");
			t_xyStack->p_Push4(m_Glyph.At(t_Id)->m_xyList.At(t_c).At(t_p0+1));
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<456>");
			t_xyStack->p_Push4(m_Glyph.At(t_Id)->m_xyList.At(t_c).At(t_p0+2));
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<458>");
			if(m_Glyph.At(t_Id)->m_xyList.At(t_c).At(t_p0+2)==FLOAT(0.0) && m_Glyph.At(t_Id)->m_xyList.At(t_c).At(t_p1+2)==FLOAT(0.0)){
				DBG_BLOCK();
				DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<459>");
				Float t_tx=(m_Glyph.At(t_Id)->m_xyList.At(t_c).At(t_p0)+m_Glyph.At(t_Id)->m_xyList.At(t_c).At(t_p1))/FLOAT(2.0);
				DBG_LOCAL(t_tx,"tx")
				DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<460>");
				Float t_ty=(m_Glyph.At(t_Id)->m_xyList.At(t_c).At(t_p0+1)+m_Glyph.At(t_Id)->m_xyList.At(t_c).At(t_p1+1))/FLOAT(2.0);
				DBG_LOCAL(t_ty,"ty")
				DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<461>");
				t_xyStack->p_Push4(t_tx);
				DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<462>");
				t_xyStack->p_Push4(t_ty);
				DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<463>");
				t_xyStack->p_Push4(FLOAT(1.0));
			}
		}
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<466>");
		gc_assign(m_Glyph.At(t_Id)->m_xyList.At(t_c),t_xyStack->p_ToArray());
	}
	return 0;
}
Array<Float > c_TFont::p_CalculateCurve(int t_x1,int t_y1,int t_x2,int t_y2,int t_x3,int t_y3){
	DBG_ENTER("TFont.CalculateCurve")
	c_TFont *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_x1,"x1")
	DBG_LOCAL(t_y1,"y1")
	DBG_LOCAL(t_x2,"x2")
	DBG_LOCAL(t_y2,"y2")
	DBG_LOCAL(t_x3,"x3")
	DBG_LOCAL(t_y3,"y3")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<495>");
	Array<Float > t_Lst=Array<Float >(10);
	DBG_LOCAL(t_Lst,"Lst")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<496>");
	int t_Counter=0;
	DBG_LOCAL(t_Counter,"Counter")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<497>");
	for(Float t_t=FLOAT(0.0);t_t<=FLOAT(0.8);t_t=t_t+FLOAT(0.2)){
		DBG_BLOCK();
		DBG_LOCAL(t_t,"t")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<498>");
		Float t_tx=(Float)pow(FLOAT(1.0)-t_t,FLOAT(2.0))*Float(t_x1)+FLOAT(2.0)*((FLOAT(1.0)-t_t)*t_t*Float(t_x2))+(Float)pow(t_t,FLOAT(2.0))*Float(t_x3);
		DBG_LOCAL(t_tx,"tx")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<499>");
		Float t_ty=(Float)pow(FLOAT(1.0)-t_t,FLOAT(2.0))*Float(t_y1)+FLOAT(2.0)*((FLOAT(1.0)-t_t)*t_t*Float(t_y2))+(Float)pow(t_t,FLOAT(2.0))*Float(t_y3);
		DBG_LOCAL(t_ty,"ty")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<500>");
		t_Lst.At(t_Counter)=t_tx;
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<501>");
		t_Lst.At(t_Counter+1)=t_ty;
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<502>");
		t_Counter=t_Counter+2;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<504>");
	return t_Lst;
}
int c_TFont::p_SmoothGlyph(int t_Id){
	DBG_ENTER("TFont.SmoothGlyph")
	c_TFont *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_Id,"Id")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<470>");
	for(int t_c=0;t_c<=m_Glyph.At(t_Id)->m_ContourNumber-1;t_c=t_c+1){
		DBG_BLOCK();
		DBG_LOCAL(t_c,"c")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<471>");
		c_Stack2* t_xyStack=(new c_Stack2)->m_new();
		DBG_LOCAL(t_xyStack,"xyStack")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<472>");
		for(int t_p0=0;t_p0<=m_Glyph.At(t_Id)->m_xyList.At(t_c).Length()-1;t_p0=t_p0+3){
			DBG_BLOCK();
			DBG_LOCAL(t_p0,"p0")
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<473>");
			int t_p1=t_p0+3;
			if(t_p1>m_Glyph.At(t_Id)->m_xyList.At(t_c).Length()-1){
				DBG_BLOCK();
				t_p1=0;
			}
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<474>");
			int t_p2=t_p1+3;
			if(t_p2>m_Glyph.At(t_Id)->m_xyList.At(t_c).Length()-1){
				DBG_BLOCK();
				t_p2=0;
			}
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<476>");
			if(m_Glyph.At(t_Id)->m_xyList.At(t_c).At(t_p0+2)==FLOAT(0.0)){
				DBG_BLOCK();
				continue;
			}
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<479>");
			if(m_Glyph.At(t_Id)->m_xyList.At(t_c).At(t_p0+2)==FLOAT(1.0) && m_Glyph.At(t_Id)->m_xyList.At(t_c).At(t_p1+2)==FLOAT(1.0)){
				DBG_BLOCK();
				DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<480>");
				t_xyStack->p_Push4(m_Glyph.At(t_Id)->m_xyList.At(t_c).At(t_p0));
				DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<481>");
				t_xyStack->p_Push4(m_Glyph.At(t_Id)->m_xyList.At(t_c).At(t_p0+1));
			}else{
				DBG_BLOCK();
				DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<484>");
				Array<Float > t_T=p_CalculateCurve(int(m_Glyph.At(t_Id)->m_xyList.At(t_c).At(t_p0)),int(m_Glyph.At(t_Id)->m_xyList.At(t_c).At(t_p0+1)),int(m_Glyph.At(t_Id)->m_xyList.At(t_c).At(t_p1)),int(m_Glyph.At(t_Id)->m_xyList.At(t_c).At(t_p1+1)),int(m_Glyph.At(t_Id)->m_xyList.At(t_c).At(t_p2)),int(m_Glyph.At(t_Id)->m_xyList.At(t_c).At(t_p2+1)));
				DBG_LOCAL(t_T,"T")
				DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<485>");
				Array<Float > t_=t_T;
				int t_2=0;
				while(t_2<t_.Length()){
					DBG_BLOCK();
					Float t_tt=t_.At(t_2);
					t_2=t_2+1;
					DBG_LOCAL(t_tt,"tt")
					DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<486>");
					t_xyStack->p_Push4(t_tt);
				}
			}
		}
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<490>");
		gc_assign(m_Glyph.At(t_Id)->m_xyList.At(t_c),t_xyStack->p_ToArray());
	}
	return 0;
}
c_TFont* c_TFont::m_new(String t_Path,int t_Size,Array<int > t_Color){
	DBG_ENTER("TFont.new")
	c_TFont *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_Path,"Path")
	DBG_LOCAL(t_Size,"Size")
	DBG_LOCAL(t_Color,"Color")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<29>");
	gc_assign(m_Stream,(new c_DataStream)->m_new(String(L"monkey://data/",14)+t_Path,true));
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<30>");
	this->m_Size=t_Size;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<31>");
	gc_assign(this->m_Color,t_Color);
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<32>");
	this->m_Path=t_Path;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<33>");
	if(m_Stream->m_Buffer==0){
		DBG_BLOCK();
		bbError(t_Path+String(L" : Font file not found",22));
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<36>");
	int t_sfntVersion=int(m_Stream->p_ReadFixed32());
	DBG_LOCAL(t_sfntVersion,"sfntVersion")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<37>");
	int t_numTables=m_Stream->p_ReadUInt(2);
	DBG_LOCAL(t_numTables,"numTables")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<38>");
	int t_searchRange=m_Stream->p_ReadUInt(2);
	DBG_LOCAL(t_searchRange,"searchRange")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<39>");
	int t_entrySelector=m_Stream->p_ReadUInt(2);
	DBG_LOCAL(t_entrySelector,"entrySelector")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<40>");
	int t_rangeShift=m_Stream->p_ReadUInt(2);
	DBG_LOCAL(t_rangeShift,"rangeShift")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<41>");
	if(t_sfntVersion==1){
		DBG_BLOCK();
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<42>");
		m_OutlineType=String(L"TT",2);
	}else{
		DBG_BLOCK();
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<44>");
		m_OutlineType=String(L"OT",2);
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<48>");
	int t_cmapOffset=0;
	int t_headOffset=0;
	int t_hheaOffset=0;
	int t_hmtxOffset=0;
	int t_maxpOffset=0;
	int t_nameOffset=0;
	int t_glyfOffset=0;
	int t_locaOffset=0;
	int t_CFFOffset=0;
	int t_VORGOffset=0;
	DBG_LOCAL(t_cmapOffset,"cmapOffset")
	DBG_LOCAL(t_headOffset,"headOffset")
	DBG_LOCAL(t_hheaOffset,"hheaOffset")
	DBG_LOCAL(t_hmtxOffset,"hmtxOffset")
	DBG_LOCAL(t_maxpOffset,"maxpOffset")
	DBG_LOCAL(t_nameOffset,"nameOffset")
	DBG_LOCAL(t_glyfOffset,"glyfOffset")
	DBG_LOCAL(t_locaOffset,"locaOffset")
	DBG_LOCAL(t_CFFOffset,"CFFOffset")
	DBG_LOCAL(t_VORGOffset,"VORGOffset")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<49>");
	for(int t_i=0;t_i<=t_numTables-1;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<50>");
		String t_tag=m_Stream->p_ReadString(4);
		DBG_LOCAL(t_tag,"tag")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<51>");
		int t_checksum=m_Stream->p_ReadUInt(4);
		DBG_LOCAL(t_checksum,"checksum")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<52>");
		int t_offset=m_Stream->p_ReadUInt(4);
		DBG_LOCAL(t_offset,"offset")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<53>");
		int t_length=m_Stream->p_ReadUInt(4);
		DBG_LOCAL(t_length,"length")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<54>");
		String t_1=t_tag;
		DBG_LOCAL(t_1,"1")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<55>");
		if(t_1==String(L"cmap",4)){
			DBG_BLOCK();
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<56>");
			t_cmapOffset=t_offset;
		}else{
			DBG_BLOCK();
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<57>");
			if(t_1==String(L"head",4)){
				DBG_BLOCK();
				DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<58>");
				t_headOffset=t_offset;
			}else{
				DBG_BLOCK();
				DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<59>");
				if(t_1==String(L"hhea",4)){
					DBG_BLOCK();
					DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<60>");
					t_hheaOffset=t_offset;
				}else{
					DBG_BLOCK();
					DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<61>");
					if(t_1==String(L"hmtx",4)){
						DBG_BLOCK();
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<62>");
						t_hmtxOffset=t_offset;
					}else{
						DBG_BLOCK();
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<63>");
						if(t_1==String(L"maxp",4)){
							DBG_BLOCK();
							DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<64>");
							t_maxpOffset=t_offset;
						}else{
							DBG_BLOCK();
							DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<65>");
							if(t_1==String(L"name",4)){
								DBG_BLOCK();
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<66>");
								t_nameOffset=t_offset;
							}else{
								DBG_BLOCK();
								DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<67>");
								if(t_1==String(L"glyf",4)){
									DBG_BLOCK();
									DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<68>");
									t_glyfOffset=t_offset;
								}else{
									DBG_BLOCK();
									DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<69>");
									if(t_1==String(L"loca",4)){
										DBG_BLOCK();
										DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<70>");
										t_locaOffset=t_offset;
									}else{
										DBG_BLOCK();
										DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<71>");
										if(t_1==String(L"CFF ",4)){
											DBG_BLOCK();
											DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<72>");
											t_CFFOffset=t_offset;
										}else{
											DBG_BLOCK();
											DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<73>");
											if(t_1==String(L"VORG",4)){
												DBG_BLOCK();
												DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<74>");
												t_VORGOffset=t_offset;
											}
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<80>");
	m_GlyphNumber=m_Stream->p_PeekUInt(2,t_maxpOffset+4);
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<81>");
	m_FontLimits.At(0)=m_Stream->p_PeekInt(2,t_headOffset+36);
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<82>");
	m_FontLimits.At(1)=m_Stream->p_PeekInt(2,t_headOffset+38);
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<83>");
	m_FontLimits.At(2)=m_Stream->p_PeekInt(2,t_headOffset+40);
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<84>");
	m_FontLimits.At(3)=m_Stream->p_PeekInt(2,t_headOffset+42);
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<85>");
	m_FontScale=Float(t_Size)*FLOAT(1.0)/Float(m_FontLimits.At(3));
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<86>");
	m_LineHeight=t_Size;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<87>");
	int t_LocaFormat=m_Stream->p_PeekInt(2,t_headOffset+50);
	DBG_LOCAL(t_LocaFormat,"LocaFormat")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<88>");
	int t_HMetricsNumber=m_Stream->p_PeekUInt(2,t_hheaOffset+34);
	DBG_LOCAL(t_HMetricsNumber,"HMetricsNumber")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<91>");
	gc_assign(m_GlyphId,Array<int >(1000));
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<92>");
	gc_assign(m_Glyph,Array<c_TFont_Glyph* >(m_GlyphNumber));
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<93>");
	for(int t_i2=0;t_i2<=m_GlyphNumber-1;t_i2=t_i2+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i2,"i")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<94>");
		gc_assign(m_Glyph.At(t_i2),(new c_TFont_Glyph)->m_new());
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<98>");
	p_LoadMetrics(t_hmtxOffset,t_HMetricsNumber);
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<99>");
	p_LoadCmapData(t_cmapOffset);
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<100>");
	p_LoadLoca(t_locaOffset,t_LocaFormat,t_glyfOffset);
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<103>");
	for(int t_g=0;t_g<=m_GlyphNumber-1;t_g=t_g+1){
		DBG_BLOCK();
		DBG_LOCAL(t_g,"g")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<105>");
		p_LoadGlyfData(t_g,m_Glyph.At(t_g)->m_FileAddress);
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<106>");
		p_QuadGlyph(t_g);
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<107>");
		p_SmoothGlyph(t_g);
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<111>");
	for(int t_g2=0;t_g2<=m_GlyphNumber-1;t_g2=t_g2+1){
		DBG_BLOCK();
		DBG_LOCAL(t_g2,"g")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<112>");
		gc_assign(m_Glyph.At(t_g2)->m_Poly,Array<c_TFont_Poly* >(m_Glyph.At(t_g2)->m_ContourNumber));
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<113>");
		for(int t_c=0;t_c<=m_Glyph.At(t_g2)->m_ContourNumber-1;t_c=t_c+1){
			DBG_BLOCK();
			DBG_LOCAL(t_c,"c")
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<114>");
			gc_assign(m_Glyph.At(t_g2)->m_Poly.At(t_c),(new c_TFont_Poly)->m_new(m_Glyph.At(t_g2)->m_xyList.At(t_c),m_FontScale));
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<115>");
			if(m_Glyph.At(t_g2)->m_ContourNumber==1 && !m_ClockwiseFound){
				DBG_BLOCK();
				DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<116>");
				m_FontClockwise=m_Glyph.At(t_g2)->m_Poly.At(0)->m_Clockwise;
				DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<117>");
				m_ClockwiseFound=true;
			}
		}
	}
	return this;
}
c_TFont* c_TFont::m_new2(){
	DBG_ENTER("TFont.new")
	c_TFont *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<8>");
	return this;
}
int c_TFont::p_LoadGlyphImages(){
	DBG_ENTER("TFont.LoadGlyphImages")
	c_TFont *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<508>");
	Array<Float > t_OrigColor=bb_graphics_GetColor();
	DBG_LOCAL(t_OrigColor,"OrigColor")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<510>");
	int t_BW=int(Float(m_FontLimits.At(2)-m_FontLimits.At(0))*m_FontScale+FLOAT(2.0));
	DBG_LOCAL(t_BW,"BW")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<511>");
	int t_BH=int(Float(m_FontLimits.At(3)-m_FontLimits.At(1))*m_FontScale+FLOAT(2.0));
	DBG_LOCAL(t_BH,"BH")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<512>");
	c_Image* t_BG=bb_graphics_CreateImage(t_BW,t_BH,1,c_Image::m_DefaultFlags);
	DBG_LOCAL(t_BG,"BG")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<513>");
	Array<int > t_BGPixels=Array<int >(t_BW*t_BH);
	DBG_LOCAL(t_BGPixels,"BGPixels")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<514>");
	bb_graphics_ReadPixels(t_BGPixels,0,0,t_BW,t_BH,0,0);
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<515>");
	t_BG->p_WritePixels(t_BGPixels,0,0,t_BW,t_BH,0,0);
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<517>");
	bb_graphics_PushMatrix();
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<518>");
	bb_graphics_Scale(m_FontScale,m_FontScale);
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<519>");
	for(int t_g=0;t_g<=m_GlyphNumber-1;t_g=t_g+1){
		DBG_BLOCK();
		DBG_LOCAL(t_g,"g")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<520>");
		if(m_Glyph.At(t_g)->m_ContourNumber<1){
			DBG_BLOCK();
			continue;
		}
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<521>");
		int t_W=int(Float(m_Glyph.At(t_g)->m_W)*m_FontScale+FLOAT(4.0));
		DBG_LOCAL(t_W,"W")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<522>");
		int t_H=int(Float(m_Glyph.At(t_g)->m_H)*m_FontScale+FLOAT(4.0));
		DBG_LOCAL(t_H,"H")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<523>");
		if(t_W<1 || t_H<1){
			DBG_BLOCK();
			continue;
		}
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<526>");
		bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(255.0));
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<527>");
		bb_graphics_DrawRect(FLOAT(0.0),FLOAT(0.0),Float(t_W)/m_FontScale,Float(t_H)/m_FontScale);
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<529>");
		bb_graphics_SetColor(FLOAT(0.0),FLOAT(0.0),FLOAT(0.0));
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<530>");
		for(int t_i=0;t_i<=m_Glyph.At(t_g)->m_ContourNumber-1;t_i=t_i+1){
			DBG_BLOCK();
			DBG_LOCAL(t_i,"i")
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<531>");
			if(m_Glyph.At(t_g)->m_Poly.At(t_i)->m_Clockwise==m_FontClockwise){
				DBG_BLOCK();
				DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<532>");
				m_Glyph.At(t_g)->m_Poly.At(t_i)->p_Draw();
			}
		}
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<536>");
		bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(255.0));
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<537>");
		for(int t_i2=0;t_i2<=m_Glyph.At(t_g)->m_ContourNumber-1;t_i2=t_i2+1){
			DBG_BLOCK();
			DBG_LOCAL(t_i2,"i")
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<538>");
			if(m_Glyph.At(t_g)->m_Poly.At(t_i2)->m_Clockwise!=m_FontClockwise){
				DBG_BLOCK();
				DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<539>");
				m_Glyph.At(t_g)->m_Poly.At(t_i2)->p_Draw();
			}
		}
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<544>");
		Array<int > t_pixels=Array<int >(t_W*t_H);
		DBG_LOCAL(t_pixels,"pixels")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<545>");
		bb_graphics_ReadPixels(t_pixels,0,0,t_W,t_H,0,0);
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<548>");
		for(int t_i3=0;t_i3<t_pixels.Length();t_i3=t_i3+1){
			DBG_BLOCK();
			DBG_LOCAL(t_i3,"i")
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<549>");
			int t_argb=t_pixels.At(t_i3);
			DBG_LOCAL(t_argb,"argb")
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<550>");
			int t_a=t_argb>>24&255;
			DBG_LOCAL(t_a,"a")
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<551>");
			int t_r=t_argb>>16&255;
			DBG_LOCAL(t_r,"r")
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<552>");
			int t_g2=t_argb>>8&255;
			DBG_LOCAL(t_g2,"g")
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<553>");
			int t_b=t_argb&255;
			DBG_LOCAL(t_b,"b")
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<554>");
			t_a=255-t_r;
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<555>");
			t_r=m_Color.At(0);
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<556>");
			t_g2=m_Color.At(1);
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<557>");
			t_b=m_Color.At(2);
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<558>");
			t_argb=t_a<<24|t_r<<16|t_g2<<8|t_b;
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<559>");
			t_pixels.At(t_i3)=t_argb;
		}
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<561>");
		gc_assign(m_Glyph.At(t_g)->m_Img,bb_graphics_CreateImage(t_W,t_H,1,c_Image::m_DefaultFlags));
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<562>");
		m_Glyph.At(t_g)->m_Img->p_WritePixels(t_pixels,0,0,t_W,t_H,0,0);
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<564>");
	bb_graphics_PopMatrix();
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<565>");
	bb_graphics_SetColor(FLOAT(255.0),FLOAT(255.0),FLOAT(255.0));
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<566>");
	bb_graphics_DrawImage(t_BG,FLOAT(0.0),FLOAT(0.0),0);
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<567>");
	m_ImagesLoaded=true;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<568>");
	bb_graphics_SetColor(t_OrigColor.At(0),t_OrigColor.At(1),t_OrigColor.At(2));
	return 0;
}
int c_TFont::p_TextHeight(String t_Text,int t_AdditionalLineSpace){
	DBG_ENTER("TFont.TextHeight")
	c_TFont *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_Text,"Text")
	DBG_LOCAL(t_AdditionalLineSpace,"AdditionalLineSpace")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<180>");
	int t_Height=0;
	DBG_LOCAL(t_Height,"Height")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<181>");
	int t_Lines=t_Text.Split(String(L"\n",1)).Length();
	DBG_LOCAL(t_Lines,"Lines")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<182>");
	int t_=t_Lines*m_LineHeight+t_Lines*t_AdditionalLineSpace;
	return t_;
}
int c_TFont::p_TextWidth(String t_Text,int t_AdditionalLetterSpace){
	DBG_ENTER("TFont.TextWidth")
	c_TFont *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_Text,"Text")
	DBG_LOCAL(t_AdditionalLetterSpace,"AdditionalLetterSpace")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<166>");
	int t_Width=0;
	DBG_LOCAL(t_Width,"Width")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<167>");
	Array<String > t_=t_Text.Split(String(L"\n",1));
	int t_2=0;
	while(t_2<t_.Length()){
		DBG_BLOCK();
		String t_L=t_.At(t_2);
		t_2=t_2+1;
		DBG_LOCAL(t_L,"L")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<168>");
		int t_TempWidth=0;
		DBG_LOCAL(t_TempWidth,"TempWidth")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<169>");
		String t_3=t_L;
		int t_4=0;
		while(t_4<t_3.Length()){
			DBG_BLOCK();
			int t_c=(int)t_3.At(t_4);
			t_4=t_4+1;
			DBG_LOCAL(t_c,"c")
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<170>");
			int t_Id=m_GlyphId.At(t_c);
			DBG_LOCAL(t_Id,"Id")
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<171>");
			t_TempWidth=int(Float(t_TempWidth)+Float(m_Glyph.At(t_Id)->m_Adv)*m_FontScale+Float(t_AdditionalLetterSpace));
		}
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<173>");
		if(t_TempWidth>t_Width){
			DBG_BLOCK();
			t_Width=t_TempWidth;
		}
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<176>");
	return t_Width;
}
int c_TFont::p_DrawText(String t_Text,int t_x,int t_y,int t_CenterX,int t_CenterY,int t_AdditionalLetterSpace,int t_AdditionalLineSpace){
	DBG_ENTER("TFont.DrawText")
	c_TFont *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_Text,"Text")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_CenterX,"CenterX")
	DBG_LOCAL(t_CenterY,"CenterY")
	DBG_LOCAL(t_AdditionalLetterSpace,"AdditionalLetterSpace")
	DBG_LOCAL(t_AdditionalLineSpace,"AdditionalLineSpace")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<128>");
	if(m_ImagesLoaded==false){
		DBG_BLOCK();
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<129>");
		p_LoadGlyphImages();
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<130>");
		m_ImagesLoaded=true;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<134>");
	int t_X2=t_x;
	DBG_LOCAL(t_X2,"X")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<135>");
	int t_Y2=t_y;
	DBG_LOCAL(t_Y2,"Y")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<137>");
	if(t_CenterY==1){
		DBG_BLOCK();
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<138>");
		t_Y2=t_Y2-this->p_TextHeight(t_Text,t_AdditionalLineSpace)/2;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<141>");
	Array<String > t_=t_Text.Split(String(L"\n",1));
	int t_2=0;
	while(t_2<t_.Length()){
		DBG_BLOCK();
		String t_L=t_.At(t_2);
		t_2=t_2+1;
		DBG_LOCAL(t_L,"L")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<142>");
		String t_3=t_L;
		int t_4=0;
		while(t_4<t_3.Length()){
			DBG_BLOCK();
			int t_c=(int)t_3.At(t_4);
			t_4=t_4+1;
			DBG_LOCAL(t_c,"c")
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<143>");
			int t_Id=m_GlyphId.At(t_c);
			DBG_LOCAL(t_Id,"Id")
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<144>");
			int t_tx=int(Float(t_X2)+Float(m_Glyph.At(t_Id)->m_xMin)*m_FontScale);
			DBG_LOCAL(t_tx,"tx")
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<145>");
			int t_ty=int(Float(t_Y2)-Float(m_Glyph.At(t_Id)->m_yMax)*m_FontScale+Float(m_LineHeight));
			DBG_LOCAL(t_ty,"ty")
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<147>");
			if(t_CenterX==1){
				DBG_BLOCK();
				DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<148>");
				t_tx=t_tx-this->p_TextWidth(t_L,t_AdditionalLetterSpace)/2;
			}
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<151>");
			if(t_c>32){
				DBG_BLOCK();
				DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<152>");
				if(m_Glyph.At(t_Id)->m_Img!=0){
					DBG_BLOCK();
					bb_graphics_DrawImage(m_Glyph.At(t_Id)->m_Img,Float(t_tx),Float(t_ty),0);
				}
			}
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<154>");
			t_X2=int(Float(t_X2)+Float(m_Glyph.At(t_Id)->m_Adv)*m_FontScale+Float(t_AdditionalLetterSpace));
		}
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<157>");
		t_X2=t_x;
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<158>");
		t_Y2=t_Y2+m_LineHeight+t_AdditionalLineSpace;
	}
	return 0;
}
void c_TFont::mark(){
	Object::mark();
	gc_mark_q(m_Stream);
	gc_mark_q(m_Color);
	gc_mark_q(m_FontLimits);
	gc_mark_q(m_GlyphId);
	gc_mark_q(m_Glyph);
}
String c_TFont::debug(){
	String t="(TFont)\n";
	t+=dbg_decl("Stream",&m_Stream);
	t+=dbg_decl("Size",&m_Size);
	t+=dbg_decl("Color",&m_Color);
	t+=dbg_decl("Path",&m_Path);
	t+=dbg_decl("OutlineType",&m_OutlineType);
	t+=dbg_decl("GlyphNumber",&m_GlyphNumber);
	t+=dbg_decl("FontLimits",&m_FontLimits);
	t+=dbg_decl("FontScale",&m_FontScale);
	t+=dbg_decl("LineHeight",&m_LineHeight);
	t+=dbg_decl("GlyphId",&m_GlyphId);
	t+=dbg_decl("Glyph",&m_Glyph);
	t+=dbg_decl("ImagesLoaded",&m_ImagesLoaded);
	t+=dbg_decl("FontClockwise",&m_FontClockwise);
	t+=dbg_decl("ClockwiseFound",&m_ClockwiseFound);
	return t;
}
c_DataStream::c_DataStream(){
	m_Buffer=0;
	m_Pointer=0;
	m_BigEndian=false;
}
c_DataStream* c_DataStream::m_new(String t_Path,bool t_BigEndianFormat){
	DBG_ENTER("DataStream.new")
	c_DataStream *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_Path,"Path")
	DBG_LOCAL(t_BigEndianFormat,"BigEndianFormat")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<14>");
	gc_assign(m_Buffer,c_DataBuffer::m_Load(t_Path));
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<15>");
	m_Pointer=0;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<16>");
	m_BigEndian=t_BigEndianFormat;
	return this;
}
c_DataStream* c_DataStream::m_new2(){
	DBG_ENTER("DataStream.new")
	c_DataStream *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<4>");
	return this;
}
Array<int > c_DataStream::p_ByteToArr(int t_Address){
	DBG_ENTER("DataStream.ByteToArr")
	c_DataStream *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_Address,"Address")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<121>");
	int t_I=m_Buffer->PeekByte(t_Address);
	DBG_LOCAL(t_I,"I")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<122>");
	Array<int > t_Str=Array<int >(8);
	DBG_LOCAL(t_Str,"Str")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<124>");
	if(t_I>-1){
		DBG_BLOCK();
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<126>");
		int t_D=128;
		int t_Counter=0;
		DBG_LOCAL(t_D,"D")
		DBG_LOCAL(t_Counter,"Counter")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<127>");
		while(t_I>0){
			DBG_BLOCK();
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<128>");
			if(t_I>=t_D){
				DBG_BLOCK();
				DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<129>");
				t_Str.At(t_Counter)=1;
				DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<130>");
				t_I=t_I-t_D;
			}else{
				DBG_BLOCK();
				DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<132>");
				t_Str.At(t_Counter)=0;
			}
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<134>");
			t_D=t_D/2;
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<135>");
			t_Counter=t_Counter+1;
		}
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<138>");
		while(t_Counter<8){
			DBG_BLOCK();
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<139>");
			t_Str.At(t_Counter)=0;
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<140>");
			t_Counter=t_Counter+1;
		}
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<142>");
		return t_Str;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<147>");
	t_I=t_I*-1;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<149>");
	int t_D2=128;
	int t_Counter2=0;
	DBG_LOCAL(t_D2,"D")
	DBG_LOCAL(t_Counter2,"Counter")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<150>");
	while(t_I>0){
		DBG_BLOCK();
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<151>");
		if(t_I>=t_D2){
			DBG_BLOCK();
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<152>");
			t_Str.At(t_Counter2)=1;
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<153>");
			t_I=t_I-t_D2;
		}else{
			DBG_BLOCK();
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<155>");
			t_Str.At(t_Counter2)=0;
		}
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<157>");
		t_D2=t_D2/2;
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<158>");
		t_Counter2=t_Counter2+1;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<161>");
	while(t_Str.Length()<8){
		DBG_BLOCK();
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<162>");
		t_Str.At(t_Counter2)=0;
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<163>");
		t_Counter2=t_Counter2+1;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<166>");
	for(int t_i2=7;t_i2>=0;t_i2=t_i2+-1){
		DBG_BLOCK();
		DBG_LOCAL(t_i2,"i")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<167>");
		if(t_Str.At(t_i2)==0){
			DBG_BLOCK();
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<168>");
			t_Str.At(t_i2)=1;
		}else{
			DBG_BLOCK();
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<170>");
			t_Str.At(t_i2)=0;
		}
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<174>");
	for(int t_i3=7;t_i3>=0;t_i3=t_i3+-1){
		DBG_BLOCK();
		DBG_LOCAL(t_i3,"i")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<175>");
		if(t_Str.At(t_i3)==0){
			DBG_BLOCK();
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<176>");
			t_Str.At(t_i3)=1;
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<177>");
			break;
		}else{
			DBG_BLOCK();
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<179>");
			t_Str.At(t_i3)=0;
		}
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<182>");
	return t_Str;
}
Array<int > c_DataStream::p_BytesToArr(int t_Address,int t_Count){
	DBG_ENTER("DataStream.BytesToArr")
	c_DataStream *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_Address,"Address")
	DBG_LOCAL(t_Count,"Count")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<110>");
	Array<int > t_Str=Array<int >(t_Count*8);
	int t_Counter=0;
	DBG_LOCAL(t_Str,"Str")
	DBG_LOCAL(t_Counter,"Counter")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<111>");
	for(int t_i=0;t_i<=t_Count-1;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<112>");
		Array<int > t_Byt=p_ByteToArr(t_Address+t_i);
		DBG_LOCAL(t_Byt,"Byt")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<113>");
		for(int t_c=0;t_c<=7;t_c=t_c+1){
			DBG_BLOCK();
			DBG_LOCAL(t_c,"c")
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<114>");
			t_Str.At(t_Counter)=t_Byt.At(t_c);
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<115>");
			t_Counter=t_Counter+1;
		}
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<118>");
	return t_Str;
}
Array<int > c_DataStream::m_ChangeEndian(Array<int > t_BitString){
	DBG_ENTER("DataStream.ChangeEndian")
	DBG_LOCAL(t_BitString,"BitString")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<98>");
	if(t_BitString.Length()<16){
		DBG_BLOCK();
		return t_BitString;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<99>");
	int t_t=0;
	DBG_LOCAL(t_t,"t")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<100>");
	for(int t_b=0;t_b<=(t_BitString.Length()-1)/2;t_b=t_b+8){
		DBG_BLOCK();
		DBG_LOCAL(t_b,"b")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<101>");
		for(int t_i=0;t_i<=7;t_i=t_i+1){
			DBG_BLOCK();
			DBG_LOCAL(t_i,"i")
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<102>");
			t_t=t_BitString.At(t_b+t_i);
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<103>");
			t_BitString.At(t_b+t_i)=t_BitString.At(t_BitString.Length()-8-t_b+t_i);
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<104>");
			t_BitString.At(t_BitString.Length()-8-t_b+t_i)=t_t;
		}
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<107>");
	return t_BitString;
}
int c_DataStream::m_CalculateBits(Array<int > t_BitString){
	DBG_ENTER("DataStream.CalculateBits")
	DBG_LOCAL(t_BitString,"BitString")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<197>");
	if(t_BitString.At(0)==0){
		DBG_BLOCK();
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<198>");
		int t_Rtn=0;
		int t_D=1;
		DBG_LOCAL(t_Rtn,"Rtn")
		DBG_LOCAL(t_D,"D")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<199>");
		for(int t_i=t_BitString.Length()-1;t_i>=0;t_i=t_i+-1){
			DBG_BLOCK();
			DBG_LOCAL(t_i,"i")
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<200>");
			if(t_BitString.At(t_i)==1){
				DBG_BLOCK();
				DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<201>");
				t_Rtn=t_Rtn+t_D;
			}
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<203>");
			t_D=t_D*2;
		}
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<205>");
		return t_Rtn;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<210>");
	for(int t_i2=0;t_i2<=t_BitString.Length()-1;t_i2=t_i2+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i2,"i")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<211>");
		if(t_BitString.At(t_i2)==0){
			DBG_BLOCK();
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<212>");
			t_BitString.At(t_i2)=1;
		}else{
			DBG_BLOCK();
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<214>");
			t_BitString.At(t_i2)=0;
		}
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<218>");
	for(int t_i3=t_BitString.Length()-1;t_i3>=0;t_i3=t_i3+-1){
		DBG_BLOCK();
		DBG_LOCAL(t_i3,"i")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<219>");
		if(t_BitString.At(t_i3)==0){
			DBG_BLOCK();
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<220>");
			t_BitString.At(t_i3)=1;
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<221>");
			break;
		}else{
			DBG_BLOCK();
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<223>");
			t_BitString.At(t_i3)=0;
		}
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<227>");
	int t_Rtn2=0;
	int t_D2=1;
	DBG_LOCAL(t_Rtn2,"Rtn")
	DBG_LOCAL(t_D2,"D")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<228>");
	for(int t_i4=t_BitString.Length()-1;t_i4>=0;t_i4=t_i4+-1){
		DBG_BLOCK();
		DBG_LOCAL(t_i4,"i")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<229>");
		if(t_BitString.At(t_i4)==1){
			DBG_BLOCK();
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<230>");
			t_Rtn2=t_Rtn2+t_D2;
		}
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<232>");
		t_D2=t_D2*2;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<234>");
	int t_=t_Rtn2*-1;
	return t_;
}
int c_DataStream::p_ReadInt(int t_ByteCount){
	DBG_ENTER("DataStream.ReadInt")
	c_DataStream *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_ByteCount,"ByteCount")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<31>");
	m_Pointer=m_Pointer+t_ByteCount;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<32>");
	if(!m_BigEndian){
		DBG_BLOCK();
		int t_=m_CalculateBits(m_ChangeEndian(p_BytesToArr(m_Pointer-t_ByteCount,t_ByteCount)));
		return t_;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<33>");
	int t_2=m_CalculateBits(p_BytesToArr(m_Pointer-t_ByteCount,t_ByteCount));
	return t_2;
}
Float c_DataStream::p_ReadFixed32(){
	DBG_ENTER("DataStream.ReadFixed32")
	c_DataStream *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<41>");
	Float t_=(String(p_ReadInt(2))+String(L".",1)+String(p_ReadInt(2))).ToFloat();
	return t_;
}
int c_DataStream::m_CalculateUBits(Array<int > t_BitString){
	DBG_ENTER("DataStream.CalculateUBits")
	DBG_LOCAL(t_BitString,"BitString")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<186>");
	int t_Rtn=0;
	int t_D=1;
	DBG_LOCAL(t_Rtn,"Rtn")
	DBG_LOCAL(t_D,"D")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<187>");
	for(int t_i=t_BitString.Length()-1;t_i>=0;t_i=t_i+-1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<188>");
		if(t_BitString.At(t_i)==1){
			DBG_BLOCK();
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<189>");
			t_Rtn=t_Rtn+t_D;
		}
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<191>");
		t_D=t_D*2;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<193>");
	return t_Rtn;
}
int c_DataStream::p_ReadUInt(int t_ByteCount){
	DBG_ENTER("DataStream.ReadUInt")
	c_DataStream *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_ByteCount,"ByteCount")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<36>");
	m_Pointer=m_Pointer+t_ByteCount;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<37>");
	if(!m_BigEndian){
		DBG_BLOCK();
		int t_=m_CalculateUBits(m_ChangeEndian(p_BytesToArr(m_Pointer-t_ByteCount,t_ByteCount)));
		return t_;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<38>");
	int t_2=m_CalculateUBits(p_BytesToArr(m_Pointer-t_ByteCount,t_ByteCount));
	return t_2;
}
String c_DataStream::p_ReadString(int t_ByteCount){
	DBG_ENTER("DataStream.ReadString")
	c_DataStream *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_ByteCount,"ByteCount")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<44>");
	m_Pointer=m_Pointer+t_ByteCount;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<45>");
	String t_=m_Buffer->p_PeekString(m_Pointer-t_ByteCount,t_ByteCount,String(L"utf8",4));
	return t_;
}
int c_DataStream::p_PeekUInt(int t_ByteCount,int t_Address){
	DBG_ENTER("DataStream.PeekUInt")
	c_DataStream *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_ByteCount,"ByteCount")
	DBG_LOCAL(t_Address,"Address")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<66>");
	if(!m_BigEndian){
		DBG_BLOCK();
		m_CalculateUBits(m_ChangeEndian(p_BytesToArr(t_Address,t_ByteCount)));
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<67>");
	int t_=m_CalculateUBits(p_BytesToArr(t_Address,t_ByteCount));
	return t_;
}
int c_DataStream::p_PeekInt(int t_ByteCount,int t_Address){
	DBG_ENTER("DataStream.PeekInt")
	c_DataStream *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_ByteCount,"ByteCount")
	DBG_LOCAL(t_Address,"Address")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<62>");
	if(!m_BigEndian){
		DBG_BLOCK();
		int t_=m_CalculateBits(m_ChangeEndian(p_BytesToArr(t_Address,t_ByteCount)));
		return t_;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<63>");
	int t_2=m_CalculateBits(p_BytesToArr(t_Address,t_ByteCount));
	return t_2;
}
int c_DataStream::p_SetPointer(int t_Offset){
	DBG_ENTER("DataStream.SetPointer")
	c_DataStream *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_Offset,"Offset")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<21>");
	m_Pointer=t_Offset;
	return 0;
}
Array<int > c_DataStream::p_ReadBits(int t_ByteCount){
	DBG_ENTER("DataStream.ReadBits")
	c_DataStream *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_ByteCount,"ByteCount")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<48>");
	Array<int > t_Str=p_BytesToArr(m_Pointer,t_ByteCount);
	DBG_LOCAL(t_Str,"Str")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<49>");
	m_Pointer=m_Pointer+t_ByteCount;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<50>");
	if(!m_BigEndian){
		DBG_BLOCK();
		t_Str=m_ChangeEndian(t_Str);
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<51>");
	int t_temp=0;
	DBG_LOCAL(t_temp,"temp")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<52>");
	for(int t_i=0;t_i<t_Str.Length()/2;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<53>");
		t_temp=t_Str.At(t_i);
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<54>");
		t_Str.At(t_i)=t_Str.At(t_Str.Length()-t_i-1);
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<55>");
		t_Str.At(t_Str.Length()-t_i-1)=t_temp;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontdatastream.monkey<57>");
	return t_Str;
}
void c_DataStream::mark(){
	Object::mark();
	gc_mark_q(m_Buffer);
}
String c_DataStream::debug(){
	String t="(DataStream)\n";
	t+=dbg_decl("Buffer",&m_Buffer);
	t+=dbg_decl("Pointer",&m_Pointer);
	t+=dbg_decl("BigEndian",&m_BigEndian);
	return t;
}
c_DataBuffer::c_DataBuffer(){
}
c_DataBuffer* c_DataBuffer::m_new(int t_length){
	DBG_ENTER("DataBuffer.new")
	c_DataBuffer *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_length,"length")
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<94>");
	if(!_New(t_length)){
		DBG_BLOCK();
		bbError(String(L"Allocate DataBuffer failed",26));
	}
	return this;
}
c_DataBuffer* c_DataBuffer::m_new2(){
	DBG_ENTER("DataBuffer.new")
	c_DataBuffer *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<91>");
	return this;
}
c_DataBuffer* c_DataBuffer::m_Load(String t_path){
	DBG_ENTER("DataBuffer.Load")
	DBG_LOCAL(t_path,"path")
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<289>");
	c_DataBuffer* t_buf=(new c_DataBuffer)->m_new2();
	DBG_LOCAL(t_buf,"buf")
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<290>");
	if(t_buf->_Load(t_path)){
		DBG_BLOCK();
		return t_buf;
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<291>");
	return 0;
}
void c_DataBuffer::p_PeekBytes(int t_address,Array<int > t_bytes,int t_offset,int t_count){
	DBG_ENTER("DataBuffer.PeekBytes")
	c_DataBuffer *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_address,"address")
	DBG_LOCAL(t_bytes,"bytes")
	DBG_LOCAL(t_offset,"offset")
	DBG_LOCAL(t_count,"count")
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<137>");
	if(t_address+t_count>Length()){
		DBG_BLOCK();
		t_count=Length()-t_address;
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<138>");
	if(t_offset+t_count>t_bytes.Length()){
		DBG_BLOCK();
		t_count=t_bytes.Length()-t_offset;
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<139>");
	for(int t_i=0;t_i<t_count;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<140>");
		t_bytes.At(t_offset+t_i)=PeekByte(t_address+t_i);
	}
}
Array<int > c_DataBuffer::p_PeekBytes2(int t_address,int t_count){
	DBG_ENTER("DataBuffer.PeekBytes")
	c_DataBuffer *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_address,"address")
	DBG_LOCAL(t_count,"count")
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<130>");
	if(t_address+t_count>Length()){
		DBG_BLOCK();
		t_count=Length()-t_address;
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<131>");
	Array<int > t_bytes=Array<int >(t_count);
	DBG_LOCAL(t_bytes,"bytes")
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<132>");
	p_PeekBytes(t_address,t_bytes,0,t_count);
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<133>");
	return t_bytes;
}
String c_DataBuffer::p_PeekString(int t_address,int t_count,String t_encoding){
	DBG_ENTER("DataBuffer.PeekString")
	c_DataBuffer *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_address,"address")
	DBG_LOCAL(t_count,"count")
	DBG_LOCAL(t_encoding,"encoding")
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<206>");
	String t_1=t_encoding;
	DBG_LOCAL(t_1,"1")
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<207>");
	if(t_1==String(L"utf8",4)){
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<208>");
		Array<int > t_p=p_PeekBytes2(t_address,t_count);
		DBG_LOCAL(t_p,"p")
		DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<209>");
		int t_i=0;
		int t_e=t_p.Length();
		bool t_err=false;
		DBG_LOCAL(t_i,"i")
		DBG_LOCAL(t_e,"e")
		DBG_LOCAL(t_err,"err")
		DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<210>");
		Array<int > t_q=Array<int >(t_e);
		int t_j=0;
		DBG_LOCAL(t_q,"q")
		DBG_LOCAL(t_j,"j")
		DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<211>");
		while(t_i<t_e){
			DBG_BLOCK();
			DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<212>");
			int t_c=t_p.At(t_i)&255;
			DBG_LOCAL(t_c,"c")
			DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<213>");
			t_i+=1;
			DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<214>");
			if((t_c&128)!=0){
				DBG_BLOCK();
				DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<215>");
				if((t_c&224)==192){
					DBG_BLOCK();
					DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<216>");
					if(t_i>=t_e || (t_p.At(t_i)&192)!=128){
						DBG_BLOCK();
						DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<217>");
						t_err=true;
						DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<218>");
						break;
					}
					DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<220>");
					t_c=(t_c&31)<<6|t_p.At(t_i)&63;
					DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<221>");
					t_i+=1;
				}else{
					DBG_BLOCK();
					DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<222>");
					if((t_c&240)==224){
						DBG_BLOCK();
						DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<223>");
						if(t_i+1>=t_e || (t_p.At(t_i)&192)!=128 || (t_p.At(t_i+1)&192)!=128){
							DBG_BLOCK();
							DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<224>");
							t_err=true;
							DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<225>");
							break;
						}
						DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<227>");
						t_c=(t_c&15)<<12|(t_p.At(t_i)&63)<<6|t_p.At(t_i+1)&63;
						DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<228>");
						t_i+=2;
					}else{
						DBG_BLOCK();
						DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<230>");
						t_err=true;
						DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<231>");
						break;
					}
				}
			}
			DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<234>");
			t_q.At(t_j)=t_c;
			DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<235>");
			t_j+=1;
		}
		DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<237>");
		if(t_err){
			DBG_BLOCK();
			DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<239>");
			String t_=String::FromChars(t_p);
			return t_;
		}
		DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<241>");
		if(t_j<t_e){
			DBG_BLOCK();
			t_q=t_q.Slice(0,t_j);
		}
		DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<242>");
		String t_2=String::FromChars(t_q);
		return t_2;
	}else{
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<243>");
		if(t_1==String(L"ascii",5)){
			DBG_BLOCK();
			DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<244>");
			Array<int > t_p2=p_PeekBytes2(t_address,t_count);
			DBG_LOCAL(t_p2,"p")
			DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<245>");
			for(int t_i2=0;t_i2<t_p2.Length();t_i2=t_i2+1){
				DBG_BLOCK();
				DBG_LOCAL(t_i2,"i")
				DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<246>");
				t_p2.At(t_i2)&=255;
			}
			DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<248>");
			String t_3=String::FromChars(t_p2);
			return t_3;
		}
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<251>");
	bbError(String(L"Invalid string encoding:",24)+t_encoding);
	return String();
}
String c_DataBuffer::p_PeekString2(int t_address,String t_encoding){
	DBG_ENTER("DataBuffer.PeekString")
	c_DataBuffer *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_address,"address")
	DBG_LOCAL(t_encoding,"encoding")
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<201>");
	String t_=p_PeekString(t_address,Length()-t_address,t_encoding);
	return t_;
}
void c_DataBuffer::p_CopyBytes(int t_address,c_DataBuffer* t_dst,int t_dstaddress,int t_count){
	DBG_ENTER("DataBuffer.CopyBytes")
	c_DataBuffer *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_address,"address")
	DBG_LOCAL(t_dst,"dst")
	DBG_LOCAL(t_dstaddress,"dstaddress")
	DBG_LOCAL(t_count,"count")
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<114>");
	if(t_address+t_count>Length()){
		DBG_BLOCK();
		t_count=Length()-t_address;
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<115>");
	if(t_dstaddress+t_count>t_dst->Length()){
		DBG_BLOCK();
		t_count=t_dst->Length()-t_dstaddress;
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<117>");
	if(t_dstaddress<=t_address){
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<118>");
		for(int t_i=0;t_i<t_count;t_i=t_i+1){
			DBG_BLOCK();
			DBG_LOCAL(t_i,"i")
			DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<119>");
			t_dst->PokeByte(t_dstaddress+t_i,PeekByte(t_address+t_i));
		}
	}else{
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<122>");
		for(int t_i2=t_count-1;t_i2>=0;t_i2=t_i2+-1){
			DBG_BLOCK();
			DBG_LOCAL(t_i2,"i")
			DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<123>");
			t_dst->PokeByte(t_dstaddress+t_i2,PeekByte(t_address+t_i2));
		}
	}
}
void c_DataBuffer::p_PokeBytes(int t_address,Array<int > t_bytes,int t_offset,int t_count){
	DBG_ENTER("DataBuffer.PokeBytes")
	c_DataBuffer *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_address,"address")
	DBG_LOCAL(t_bytes,"bytes")
	DBG_LOCAL(t_offset,"offset")
	DBG_LOCAL(t_count,"count")
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<145>");
	if(t_address+t_count>Length()){
		DBG_BLOCK();
		t_count=Length()-t_address;
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<146>");
	if(t_offset+t_count>t_bytes.Length()){
		DBG_BLOCK();
		t_count=t_bytes.Length()-t_offset;
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<147>");
	for(int t_i=0;t_i<t_count;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<148>");
		PokeByte(t_address+t_i,t_bytes.At(t_offset+t_i));
	}
}
int c_DataBuffer::p_PokeString(int t_address,String t_str,String t_encoding){
	DBG_ENTER("DataBuffer.PokeString")
	c_DataBuffer *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_address,"address")
	DBG_LOCAL(t_str,"str")
	DBG_LOCAL(t_encoding,"encoding")
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<256>");
	String t_2=t_encoding;
	DBG_LOCAL(t_2,"2")
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<257>");
	if(t_2==String(L"utf8",4)){
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<258>");
		Array<int > t_p=t_str.ToChars();
		DBG_LOCAL(t_p,"p")
		DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<259>");
		int t_i=0;
		int t_e=t_p.Length();
		DBG_LOCAL(t_i,"i")
		DBG_LOCAL(t_e,"e")
		DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<260>");
		Array<int > t_q=Array<int >(t_e*3);
		int t_j=0;
		DBG_LOCAL(t_q,"q")
		DBG_LOCAL(t_j,"j")
		DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<261>");
		while(t_i<t_e){
			DBG_BLOCK();
			DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<262>");
			int t_c=t_p.At(t_i)&65535;
			DBG_LOCAL(t_c,"c")
			DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<263>");
			t_i+=1;
			DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<264>");
			if(t_c<128){
				DBG_BLOCK();
				DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<265>");
				t_q.At(t_j)=t_c;
				DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<266>");
				t_j+=1;
			}else{
				DBG_BLOCK();
				DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<267>");
				if(t_c<2048){
					DBG_BLOCK();
					DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<268>");
					t_q.At(t_j)=192|t_c>>6;
					DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<269>");
					t_q.At(t_j+1)=128|t_c&63;
					DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<270>");
					t_j+=2;
				}else{
					DBG_BLOCK();
					DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<272>");
					t_q.At(t_j)=224|t_c>>12;
					DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<273>");
					t_q.At(t_j+1)=128|t_c>>6&63;
					DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<274>");
					t_q.At(t_j+2)=128|t_c&63;
					DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<275>");
					t_j+=3;
				}
			}
		}
		DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<278>");
		p_PokeBytes(t_address,t_q,0,t_j);
		DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<279>");
		return t_j;
	}else{
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<280>");
		if(t_2==String(L"ascii",5)){
			DBG_BLOCK();
			DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<281>");
			p_PokeBytes(t_address,t_str.ToChars(),0,t_str.Length());
			DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<282>");
			int t_=t_str.Length();
			return t_;
		}
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/databuffer.monkey<285>");
	bbError(String(L"Invalid string encoding:",24)+t_encoding);
	return 0;
}
void c_DataBuffer::mark(){
	BBDataBuffer::mark();
}
String c_DataBuffer::debug(){
	String t="(DataBuffer)\n";
	return t;
}
c_TFont_Glyph::c_TFont_Glyph(){
	m_Adv=0;
	m_Lsb=0;
	m_FileAddress=0;
	m_ContourNumber=0;
	m_xMin=0;
	m_yMin=0;
	m_xMax=0;
	m_yMax=0;
	m_W=0;
	m_H=0;
	m_xyList=Array<Array<Float > >();
	m_Poly=Array<c_TFont_Poly* >();
	m_Img=0;
}
c_TFont_Glyph* c_TFont_Glyph::m_new(){
	DBG_ENTER("TFont_Glyph.new")
	c_TFont_Glyph *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfont.monkey<576>");
	return this;
}
void c_TFont_Glyph::mark(){
	Object::mark();
	gc_mark_q(m_xyList);
	gc_mark_q(m_Poly);
	gc_mark_q(m_Img);
}
String c_TFont_Glyph::debug(){
	String t="(TFont_Glyph)\n";
	t+=dbg_decl("Adv",&m_Adv);
	t+=dbg_decl("Lsb",&m_Lsb);
	t+=dbg_decl("FileAddress",&m_FileAddress);
	t+=dbg_decl("xMin",&m_xMin);
	t+=dbg_decl("yMin",&m_yMin);
	t+=dbg_decl("xMax",&m_xMax);
	t+=dbg_decl("yMax",&m_yMax);
	t+=dbg_decl("W",&m_W);
	t+=dbg_decl("H",&m_H);
	t+=dbg_decl("ContourNumber",&m_ContourNumber);
	t+=dbg_decl("xyList",&m_xyList);
	t+=dbg_decl("Poly",&m_Poly);
	t+=dbg_decl("Img",&m_Img);
	return t;
}
c_Stack2::c_Stack2(){
	m_data=Array<Float >();
	m_length=0;
}
c_Stack2* c_Stack2::m_new(){
	DBG_ENTER("Stack.new")
	c_Stack2 *self=this;
	DBG_LOCAL(self,"Self")
	return this;
}
c_Stack2* c_Stack2::m_new2(Array<Float > t_data){
	DBG_ENTER("Stack.new")
	c_Stack2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<13>");
	gc_assign(this->m_data,t_data.Slice(0));
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<14>");
	this->m_length=t_data.Length();
	return this;
}
void c_Stack2::p_Push4(Float t_value){
	DBG_ENTER("Stack.Push")
	c_Stack2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<71>");
	if(m_length==m_data.Length()){
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<72>");
		gc_assign(m_data,m_data.Resize(m_length*2+10));
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<74>");
	m_data.At(m_length)=t_value;
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<75>");
	m_length+=1;
}
void c_Stack2::p_Push5(Array<Float > t_values,int t_offset,int t_count){
	DBG_ENTER("Stack.Push")
	c_Stack2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_values,"values")
	DBG_LOCAL(t_offset,"offset")
	DBG_LOCAL(t_count,"count")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<83>");
	for(int t_i=0;t_i<t_count;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<84>");
		p_Push4(t_values.At(t_offset+t_i));
	}
}
void c_Stack2::p_Push6(Array<Float > t_values,int t_offset){
	DBG_ENTER("Stack.Push")
	c_Stack2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_values,"values")
	DBG_LOCAL(t_offset,"offset")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<79>");
	p_Push5(t_values,t_offset,t_values.Length()-t_offset);
}
Array<Float > c_Stack2::p_ToArray(){
	DBG_ENTER("Stack.ToArray")
	c_Stack2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<18>");
	Array<Float > t_t=Array<Float >(m_length);
	DBG_LOCAL(t_t,"t")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<19>");
	for(int t_i=0;t_i<m_length;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<20>");
		t_t.At(t_i)=m_data.At(t_i);
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<22>");
	return t_t;
}
Float c_Stack2::m_NIL;
void c_Stack2::p_Clear(){
	DBG_ENTER("Stack.Clear")
	c_Stack2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<34>");
	for(int t_i=0;t_i<m_length;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<35>");
		m_data.At(t_i)=m_NIL;
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<37>");
	m_length=0;
}
void c_Stack2::p_Length(int t_newlength){
	DBG_ENTER("Stack.Length")
	c_Stack2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_newlength,"newlength")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<45>");
	if(t_newlength<m_length){
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<46>");
		for(int t_i=t_newlength;t_i<m_length;t_i=t_i+1){
			DBG_BLOCK();
			DBG_LOCAL(t_i,"i")
			DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<47>");
			m_data.At(t_i)=m_NIL;
		}
	}else{
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<49>");
		if(t_newlength>m_data.Length()){
			DBG_BLOCK();
			DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<50>");
			gc_assign(m_data,m_data.Resize(bb_math_Max(m_length*2+10,t_newlength)));
		}
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<52>");
	m_length=t_newlength;
}
int c_Stack2::p_Length2(){
	DBG_ENTER("Stack.Length")
	c_Stack2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<56>");
	return m_length;
}
Float c_Stack2::p_Get(int t_index){
	DBG_ENTER("Stack.Get")
	c_Stack2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_index,"index")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<104>");
	return m_data.At(t_index);
}
void c_Stack2::p_Remove4(int t_index){
	DBG_ENTER("Stack.Remove")
	c_Stack2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_index,"index")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<137>");
	for(int t_i=t_index;t_i<m_length-1;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<138>");
		m_data.At(t_i)=m_data.At(t_i+1);
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<140>");
	m_length-=1;
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<141>");
	m_data.At(m_length)=m_NIL;
}
void c_Stack2::mark(){
	Object::mark();
	gc_mark_q(m_data);
}
String c_Stack2::debug(){
	String t="(Stack)\n";
	t+=dbg_decl("NIL",&c_Stack2::m_NIL);
	t+=dbg_decl("data",&m_data);
	t+=dbg_decl("length",&m_length);
	return t;
}
c_TFont_Poly::c_TFont_Poly(){
	m_Scaler=FLOAT(.0);
	m_OrigArray=Array<Float >();
	m_xyList=Array<Float >();
	m_Clockwise=false;
	m_Triangles=(new c_Stack3)->m_new();
}
bool c_TFont_Poly::p_GetClockWise(){
	DBG_ENTER("TFont_Poly.GetClockWise")
	c_TFont_Poly *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<61>");
	int t_Total=0;
	DBG_LOCAL(t_Total,"Total")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<62>");
	for(int t_p1=0;t_p1<m_xyList.Length()/2;t_p1=t_p1+1){
		DBG_BLOCK();
		DBG_LOCAL(t_p1,"p1")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<63>");
		int t_p2=t_p1+1;
		if(t_p2==m_xyList.Length()/2){
			DBG_BLOCK();
			t_p2=0;
		}
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<64>");
		t_Total=int(Float(t_Total)+(m_xyList.At(t_p2*2)-m_xyList.At(t_p1*2))*(m_xyList.At(t_p2*2+1)+m_xyList.At(t_p1*2+1)));
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<66>");
	if(t_Total<0){
		DBG_BLOCK();
		return true;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<67>");
	return false;
}
c_FloatStack* c_TFont_Poly::m_points;
void c_TFont_Poly::p_BuildPoints(){
	DBG_ENTER("TFont_Poly.BuildPoints")
	c_TFont_Poly *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<41>");
	m_points->p_Clear();
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<42>");
	for(int t_i=0;t_i<m_xyList.Length();t_i=t_i+2){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<43>");
		int t_idx=t_i;
		DBG_LOCAL(t_idx,"idx")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<44>");
		if(!m_Clockwise){
			DBG_BLOCK();
			t_idx=m_xyList.Length()-t_i-2;
		}
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<45>");
		bool t_found=false;
		DBG_LOCAL(t_found,"found")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<46>");
		int t_x=int(m_xyList.At(t_idx));
		DBG_LOCAL(t_x,"x")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<47>");
		int t_y=int(m_xyList.At(t_idx+1));
		DBG_LOCAL(t_y,"y")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<48>");
		for(int t_j=0;t_j<m_points->p_Length2();t_j=t_j+2){
			DBG_BLOCK();
			DBG_LOCAL(t_j,"j")
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<49>");
			if(int(Float(t_x)*m_Scaler)==int(m_points->p_Get(t_j)*m_Scaler) && int(Float(t_y)*m_Scaler)==int(m_points->p_Get(t_j+1)*m_Scaler)){
				DBG_BLOCK();
				DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<50>");
				t_found=true;
				DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<51>");
				break;
			}
		}
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<54>");
		if(!t_found){
			DBG_BLOCK();
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<55>");
			m_points->p_Push4(Float(t_x));
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<56>");
			m_points->p_Push4(Float(t_y));
		}
	}
}
Float c_TFont_Poly::p_Heading(Float t_x0,Float t_y0,Float t_x1,Float t_y1){
	DBG_ENTER("TFont_Poly.Heading")
	c_TFont_Poly *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_x0,"x0")
	DBG_LOCAL(t_y0,"y0")
	DBG_LOCAL(t_x1,"x1")
	DBG_LOCAL(t_y1,"y1")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<93>");
	Float t_=(Float)(atan2(t_y1-t_y0,t_x1-t_x0)*R2D);
	return t_;
}
Float c_TFont_Poly::p_Angle(int t_p1){
	DBG_ENTER("TFont_Poly.Angle")
	c_TFont_Poly *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_p1,"p1")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<79>");
	int t_p0=t_p1-1;
	if(t_p0==-1){
		DBG_BLOCK();
		t_p0=m_points->p_Length2()/2-1;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<80>");
	int t_p2=t_p1+1;
	if(t_p2==m_points->p_Length2()/2){
		DBG_BLOCK();
		t_p2=0;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<81>");
	Float t_head0=p_Heading(m_points->p_Get(t_p0*2),m_points->p_Get(t_p0*2+1),m_points->p_Get(t_p1*2),m_points->p_Get(t_p1*2+1));
	DBG_LOCAL(t_head0,"head0")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<82>");
	Float t_head1=p_Heading(m_points->p_Get(t_p1*2),m_points->p_Get(t_p1*2+1),m_points->p_Get(t_p2*2),m_points->p_Get(t_p2*2+1));
	DBG_LOCAL(t_head1,"head1")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<83>");
	Float t_delta=t_head1-t_head0;
	DBG_LOCAL(t_delta,"delta")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<84>");
	while(t_delta>FLOAT(180.0)){
		DBG_BLOCK();
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<85>");
		t_delta=t_delta-FLOAT(360.0);
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<87>");
	while(t_delta<FLOAT(-180.0)){
		DBG_BLOCK();
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<88>");
		t_delta=t_delta+FLOAT(360.0);
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<90>");
	return t_delta;
}
Float c_TFont_Poly::p_DotProduct(int t_p0,int t_p1,int t_p2){
	DBG_ENTER("TFont_Poly.DotProduct")
	c_TFont_Poly *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_p0,"p0")
	DBG_LOCAL(t_p1,"p1")
	DBG_LOCAL(t_p2,"p2")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<70>");
	int t_vx0=int(m_points->p_Get(t_p0*2));
	DBG_LOCAL(t_vx0,"vx0")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<71>");
	int t_vy0=int(m_points->p_Get(t_p0*2+1));
	DBG_LOCAL(t_vy0,"vy0")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<72>");
	int t_vx2=int(m_points->p_Get(t_p2*2));
	DBG_LOCAL(t_vx2,"vx2")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<73>");
	int t_vy2=int(m_points->p_Get(t_p2*2+1));
	DBG_LOCAL(t_vy2,"vy2")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<74>");
	int t_vx1=int(m_points->p_Get(t_p1*2));
	DBG_LOCAL(t_vx1,"vx1")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<75>");
	int t_vy1=int(m_points->p_Get(t_p1*2+1));
	DBG_LOCAL(t_vy1,"vy1")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<76>");
	Float t_=Float((t_vx1-t_vx0)*(t_vy2-t_vy1)-(t_vx2-t_vx1)*(t_vy1-t_vy0));
	return t_;
}
bool c_TFont_Poly::p_AnyInside(int t_p0,int t_p1,int t_p2){
	DBG_ENTER("TFont_Poly.AnyInside")
	c_TFont_Poly *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_p0,"p0")
	DBG_LOCAL(t_p1,"p1")
	DBG_LOCAL(t_p2,"p2")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<127>");
	for(int t_i=0;t_i<m_points->p_Length2()/2;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<128>");
		if(t_i!=t_p0 && t_i!=t_p1 && t_i!=t_p2){
			DBG_BLOCK();
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<129>");
			if(p_DotProduct(t_p0,t_p1,t_i)>FLOAT(0.0)){
				DBG_BLOCK();
				DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<130>");
				if(p_DotProduct(t_p1,t_p2,t_i)>=FLOAT(0.0)){
					DBG_BLOCK();
					DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<131>");
					if(p_DotProduct(t_p2,t_p0,t_i)>FLOAT(0.0)){
						DBG_BLOCK();
						DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<132>");
						return true;
					}
				}
			}
		}
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<138>");
	return false;
}
int c_TFont_Poly::p_Triangulate(){
	DBG_ENTER("TFont_Poly.Triangulate")
	c_TFont_Poly *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<97>");
	while(m_points->p_Length2()>4){
		DBG_BLOCK();
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<99>");
		int t_ExitFlag=1;
		DBG_LOCAL(t_ExitFlag,"ExitFlag")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<100>");
		for(int t_i=0;t_i<m_points->p_Length2()/2;t_i=t_i+1){
			DBG_BLOCK();
			DBG_LOCAL(t_i,"i")
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<101>");
			Float t_Ang=p_Angle(t_i);
			DBG_LOCAL(t_Ang,"Ang")
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<102>");
			if(t_Ang<FLOAT(180.0) && t_Ang>FLOAT(0.0)){
				DBG_BLOCK();
				t_ExitFlag=0;
				break;
			}
		}
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<104>");
		if(t_ExitFlag==1){
			DBG_BLOCK();
			break;
		}
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<106>");
		int t_Found=0;
		DBG_LOCAL(t_Found,"Found")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<107>");
		for(int t_p1=0;t_p1<m_points->p_Length2()/2;t_p1=t_p1+1){
			DBG_BLOCK();
			DBG_LOCAL(t_p1,"p1")
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<108>");
			Float t_Ang2=p_Angle(t_p1);
			DBG_LOCAL(t_Ang2,"Ang")
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<109>");
			if(t_Ang2<FLOAT(180.0) && t_Ang2>FLOAT(0.0)){
				DBG_BLOCK();
				DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<110>");
				int t_p0=t_p1-1;
				if(t_p0==-1){
					DBG_BLOCK();
					t_p0=m_points->p_Length2()/2-1;
				}
				DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<111>");
				int t_p2=t_p1+1;
				if(t_p2==m_points->p_Length2()/2){
					DBG_BLOCK();
					t_p2=0;
				}
				DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<112>");
				if(!p_AnyInside(t_p0,t_p1,t_p2)){
					DBG_BLOCK();
					DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<113>");
					Float t_[]={m_points->p_Get(t_p0*2),m_points->p_Get(t_p0*2+1),m_points->p_Get(t_p1*2),m_points->p_Get(t_p1*2+1),m_points->p_Get(t_p2*2),m_points->p_Get(t_p2*2+1)};
					m_Triangles->p_Push7(Array<Float >(t_,6));
					DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<114>");
					m_points->p_Remove4(t_p1*2);
					DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<115>");
					m_points->p_Remove4(t_p1*2);
					DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<116>");
					t_Found=1;
					DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<117>");
					break;
				}
			}
		}
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<122>");
		if(t_Found==0){
			DBG_BLOCK();
			break;
		}
	}
	return 0;
}
c_TFont_Poly* c_TFont_Poly::m_new(Array<Float > t_xyList,Float t_Scaler){
	DBG_ENTER("TFont_Poly.new")
	c_TFont_Poly *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_xyList,"xyList")
	DBG_LOCAL(t_Scaler,"Scaler")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<28>");
	this->m_Scaler=t_Scaler;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<29>");
	gc_assign(this->m_OrigArray,t_xyList);
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<30>");
	gc_assign(this->m_xyList,t_xyList);
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<33>");
	this->m_Clockwise=p_GetClockWise();
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<35>");
	p_BuildPoints();
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<36>");
	p_Triangulate();
	return this;
}
c_TFont_Poly* c_TFont_Poly::m_new2(){
	DBG_ENTER("TFont_Poly.new")
	c_TFont_Poly *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<3>");
	return this;
}
int c_TFont_Poly::p_Draw(){
	DBG_ENTER("TFont_Poly.Draw")
	c_TFont_Poly *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<14>");
	for(int t_i=0;t_i<m_Triangles->p_Length2();t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<15>");
		if(m_Clockwise){
			DBG_BLOCK();
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<16>");
			bb_graphics_DrawPoly(m_Triangles->p_Get(t_i));
		}else{
			DBG_BLOCK();
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/tfontpoly.monkey<18>");
			bb_graphics_DrawPoly(m_Triangles->p_Get(m_Triangles->p_Length2()-t_i-1));
		}
	}
	return 0;
}
void c_TFont_Poly::mark(){
	Object::mark();
	gc_mark_q(m_OrigArray);
	gc_mark_q(m_xyList);
	gc_mark_q(m_Triangles);
}
String c_TFont_Poly::debug(){
	String t="(TFont_Poly)\n";
	t+=dbg_decl("Clockwise",&m_Clockwise);
	t+=dbg_decl("Scaler",&m_Scaler);
	t+=dbg_decl("Triangles",&m_Triangles);
	t+=dbg_decl("xyList",&m_xyList);
	t+=dbg_decl("OrigArray",&m_OrigArray);
	t+=dbg_decl("points",&c_TFont_Poly::m_points);
	return t;
}
c_FloatStack::c_FloatStack(){
}
c_FloatStack* c_FloatStack::m_new(Array<Float > t_data){
	DBG_ENTER("FloatStack.new")
	c_FloatStack *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<338>");
	c_Stack2::m_new2(t_data);
	return this;
}
c_FloatStack* c_FloatStack::m_new2(){
	DBG_ENTER("FloatStack.new")
	c_FloatStack *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<335>");
	c_Stack2::m_new();
	return this;
}
void c_FloatStack::mark(){
	c_Stack2::mark();
}
String c_FloatStack::debug(){
	String t="(FloatStack)\n";
	t=c_Stack2::debug()+t;
	return t;
}
int bb_math_Max(int t_x,int t_y){
	DBG_ENTER("Max")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/math.monkey<56>");
	if(t_x>t_y){
		DBG_BLOCK();
		return t_x;
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/math.monkey<57>");
	return t_y;
}
Float bb_math_Max2(Float t_x,Float t_y){
	DBG_ENTER("Max")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/math.monkey<83>");
	if(t_x>t_y){
		DBG_BLOCK();
		return t_x;
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/math.monkey<84>");
	return t_y;
}
c_Stack3::c_Stack3(){
	m_data=Array<Array<Float > >();
	m_length=0;
}
c_Stack3* c_Stack3::m_new(){
	DBG_ENTER("Stack.new")
	c_Stack3 *self=this;
	DBG_LOCAL(self,"Self")
	return this;
}
c_Stack3* c_Stack3::m_new2(Array<Array<Float > > t_data){
	DBG_ENTER("Stack.new")
	c_Stack3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<13>");
	gc_assign(this->m_data,t_data.Slice(0));
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<14>");
	this->m_length=t_data.Length();
	return this;
}
void c_Stack3::p_Push7(Array<Float > t_value){
	DBG_ENTER("Stack.Push")
	c_Stack3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<71>");
	if(m_length==m_data.Length()){
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<72>");
		gc_assign(m_data,m_data.Resize(m_length*2+10));
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<74>");
	gc_assign(m_data.At(m_length),t_value);
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<75>");
	m_length+=1;
}
void c_Stack3::p_Push8(Array<Array<Float > > t_values,int t_offset,int t_count){
	DBG_ENTER("Stack.Push")
	c_Stack3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_values,"values")
	DBG_LOCAL(t_offset,"offset")
	DBG_LOCAL(t_count,"count")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<83>");
	for(int t_i=0;t_i<t_count;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<84>");
		p_Push7(t_values.At(t_offset+t_i));
	}
}
void c_Stack3::p_Push9(Array<Array<Float > > t_values,int t_offset){
	DBG_ENTER("Stack.Push")
	c_Stack3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_values,"values")
	DBG_LOCAL(t_offset,"offset")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<79>");
	p_Push8(t_values,t_offset,t_values.Length()-t_offset);
}
Array<Float > c_Stack3::m_NIL;
void c_Stack3::p_Length(int t_newlength){
	DBG_ENTER("Stack.Length")
	c_Stack3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_newlength,"newlength")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<45>");
	if(t_newlength<m_length){
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<46>");
		for(int t_i=t_newlength;t_i<m_length;t_i=t_i+1){
			DBG_BLOCK();
			DBG_LOCAL(t_i,"i")
			DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<47>");
			gc_assign(m_data.At(t_i),m_NIL);
		}
	}else{
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<49>");
		if(t_newlength>m_data.Length()){
			DBG_BLOCK();
			DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<50>");
			gc_assign(m_data,m_data.Resize(bb_math_Max(m_length*2+10,t_newlength)));
		}
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<52>");
	m_length=t_newlength;
}
int c_Stack3::p_Length2(){
	DBG_ENTER("Stack.Length")
	c_Stack3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<56>");
	return m_length;
}
Array<Float > c_Stack3::p_Get(int t_index){
	DBG_ENTER("Stack.Get")
	c_Stack3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_index,"index")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<104>");
	return m_data.At(t_index);
}
void c_Stack3::mark(){
	Object::mark();
	gc_mark_q(m_data);
}
String c_Stack3::debug(){
	String t="(Stack)\n";
	t+=dbg_decl("NIL",&c_Stack3::m_NIL);
	t+=dbg_decl("data",&m_data);
	t+=dbg_decl("length",&m_length);
	return t;
}
c_TFont* bb_shooter_lbfont;
int bb_audio_PlayMusic(String t_path,int t_flags){
	DBG_ENTER("PlayMusic")
	DBG_LOCAL(t_path,"path")
	DBG_LOCAL(t_flags,"flags")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/audio.monkey<85>");
	int t_=bb_audio_device->PlayMusic(bb_data_FixDataPath(t_path),t_flags);
	return t_;
}
int bb_audio_ResumeMusic(){
	DBG_ENTER("ResumeMusic")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/audio.monkey<97>");
	bb_audio_device->ResumeMusic();
	return 0;
}
int bb_input_KeyHit(int t_key){
	DBG_ENTER("KeyHit")
	DBG_LOCAL(t_key,"key")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/input.monkey<44>");
	int t_=bb_input_device->p_KeyHit(t_key);
	return t_;
}
int bb_audio_PauseMusic(){
	DBG_ENTER("PauseMusic")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/audio.monkey<93>");
	bb_audio_device->PauseMusic();
	return 0;
}
c_Stream::c_Stream(){
}
c_Stream* c_Stream::m_new(){
	DBG_ENTER("Stream.new")
	c_Stream *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/stream.monkey<31>");
	return this;
}
void c_Stream::p_ReadError(){
	DBG_ENTER("Stream.ReadError")
	c_Stream *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/stream.monkey<188>");
	throw (new c_StreamReadError)->m_new(this);
}
void c_Stream::p_ReadAll(c_DataBuffer* t_buffer,int t_offset,int t_count){
	DBG_ENTER("Stream.ReadAll")
	c_Stream *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_buffer,"buffer")
	DBG_LOCAL(t_offset,"offset")
	DBG_LOCAL(t_count,"count")
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/stream.monkey<48>");
	while(t_count>0){
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/brl/stream.monkey<49>");
		int t_n=p_Read(t_buffer,t_offset,t_count);
		DBG_LOCAL(t_n,"n")
		DBG_INFO("D:/MonkeyXFree84f/modules/brl/stream.monkey<50>");
		if(t_n<=0){
			DBG_BLOCK();
			p_ReadError();
		}
		DBG_INFO("D:/MonkeyXFree84f/modules/brl/stream.monkey<51>");
		t_offset+=t_n;
		DBG_INFO("D:/MonkeyXFree84f/modules/brl/stream.monkey<52>");
		t_count-=t_n;
	}
}
c_DataBuffer* c_Stream::p_ReadAll2(){
	DBG_ENTER("Stream.ReadAll")
	c_Stream *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/stream.monkey<57>");
	c_Stack4* t_bufs=(new c_Stack4)->m_new();
	DBG_LOCAL(t_bufs,"bufs")
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/stream.monkey<58>");
	c_DataBuffer* t_buf=(new c_DataBuffer)->m_new(4096);
	int t_off=0;
	int t_len=0;
	DBG_LOCAL(t_buf,"buf")
	DBG_LOCAL(t_off,"off")
	DBG_LOCAL(t_len,"len")
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/stream.monkey<59>");
	do{
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/brl/stream.monkey<60>");
		int t_n=p_Read(t_buf,t_off,4096-t_off);
		DBG_LOCAL(t_n,"n")
		DBG_INFO("D:/MonkeyXFree84f/modules/brl/stream.monkey<61>");
		if(t_n<=0){
			DBG_BLOCK();
			break;
		}
		DBG_INFO("D:/MonkeyXFree84f/modules/brl/stream.monkey<62>");
		t_off+=t_n;
		DBG_INFO("D:/MonkeyXFree84f/modules/brl/stream.monkey<63>");
		t_len+=t_n;
		DBG_INFO("D:/MonkeyXFree84f/modules/brl/stream.monkey<64>");
		if(t_off==4096){
			DBG_BLOCK();
			DBG_INFO("D:/MonkeyXFree84f/modules/brl/stream.monkey<65>");
			t_off=0;
			DBG_INFO("D:/MonkeyXFree84f/modules/brl/stream.monkey<66>");
			t_bufs->p_Push10(t_buf);
			DBG_INFO("D:/MonkeyXFree84f/modules/brl/stream.monkey<67>");
			t_buf=(new c_DataBuffer)->m_new(4096);
		}
	}while(!(false));
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/stream.monkey<70>");
	c_DataBuffer* t_data=(new c_DataBuffer)->m_new(t_len);
	DBG_LOCAL(t_data,"data")
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/stream.monkey<71>");
	t_off=0;
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/stream.monkey<72>");
	c_Enumerator* t_=t_bufs->p_ObjectEnumerator();
	while(t_->p_HasNext()){
		DBG_BLOCK();
		c_DataBuffer* t_tbuf=t_->p_NextObject();
		DBG_LOCAL(t_tbuf,"tbuf")
		DBG_INFO("D:/MonkeyXFree84f/modules/brl/stream.monkey<73>");
		t_tbuf->p_CopyBytes(0,t_data,t_off,4096);
		DBG_INFO("D:/MonkeyXFree84f/modules/brl/stream.monkey<74>");
		t_tbuf->Discard();
		DBG_INFO("D:/MonkeyXFree84f/modules/brl/stream.monkey<75>");
		t_off+=4096;
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/stream.monkey<77>");
	t_buf->p_CopyBytes(0,t_data,t_off,t_len-t_off);
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/stream.monkey<78>");
	t_buf->Discard();
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/stream.monkey<79>");
	return t_data;
}
String c_Stream::p_ReadString2(int t_count,String t_encoding){
	DBG_ENTER("Stream.ReadString")
	c_Stream *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_count,"count")
	DBG_LOCAL(t_encoding,"encoding")
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/stream.monkey<120>");
	c_DataBuffer* t_buf=(new c_DataBuffer)->m_new(t_count);
	DBG_LOCAL(t_buf,"buf")
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/stream.monkey<121>");
	p_ReadAll(t_buf,0,t_count);
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/stream.monkey<122>");
	String t_=t_buf->p_PeekString2(0,t_encoding);
	return t_;
}
String c_Stream::p_ReadString3(String t_encoding){
	DBG_ENTER("Stream.ReadString")
	c_Stream *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_encoding,"encoding")
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/stream.monkey<126>");
	c_DataBuffer* t_buf=p_ReadAll2();
	DBG_LOCAL(t_buf,"buf")
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/stream.monkey<127>");
	String t_=t_buf->p_PeekString2(0,t_encoding);
	return t_;
}
void c_Stream::p_WriteError(){
	DBG_ENTER("Stream.WriteError")
	c_Stream *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/stream.monkey<192>");
	throw (new c_StreamWriteError)->m_new(this);
}
void c_Stream::p_WriteAll(c_DataBuffer* t_buffer,int t_offset,int t_count){
	DBG_ENTER("Stream.WriteAll")
	c_Stream *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_buffer,"buffer")
	DBG_LOCAL(t_offset,"offset")
	DBG_LOCAL(t_count,"count")
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/stream.monkey<83>");
	while(t_count>0){
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/brl/stream.monkey<84>");
		int t_n=p_Write(t_buffer,t_offset,t_count);
		DBG_LOCAL(t_n,"n")
		DBG_INFO("D:/MonkeyXFree84f/modules/brl/stream.monkey<85>");
		if(t_n<=0){
			DBG_BLOCK();
			p_WriteError();
		}
		DBG_INFO("D:/MonkeyXFree84f/modules/brl/stream.monkey<86>");
		t_offset+=t_n;
		DBG_INFO("D:/MonkeyXFree84f/modules/brl/stream.monkey<87>");
		t_count-=t_n;
	}
}
void c_Stream::p_WriteString(String t_value,String t_encoding){
	DBG_ENTER("Stream.WriteString")
	c_Stream *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_LOCAL(t_encoding,"encoding")
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/stream.monkey<163>");
	c_DataBuffer* t_buf=(new c_DataBuffer)->m_new(t_value.Length()*3);
	DBG_LOCAL(t_buf,"buf")
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/stream.monkey<164>");
	int t_len=t_buf->p_PokeString(0,t_value,String(L"utf8",4));
	DBG_LOCAL(t_len,"len")
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/stream.monkey<165>");
	p_WriteAll(t_buf,0,t_len);
}
void c_Stream::mark(){
	Object::mark();
}
String c_Stream::debug(){
	String t="(Stream)\n";
	return t;
}
c_FileStream::c_FileStream(){
	m__stream=0;
}
BBFileStream* c_FileStream::m_OpenStream(String t_path,String t_mode){
	DBG_ENTER("FileStream.OpenStream")
	DBG_LOCAL(t_path,"path")
	DBG_LOCAL(t_mode,"mode")
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/filestream.monkey<79>");
	BBFileStream* t_stream=(new BBFileStream);
	DBG_LOCAL(t_stream,"stream")
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/filestream.monkey<80>");
	String t_fmode=t_mode;
	DBG_LOCAL(t_fmode,"fmode")
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/filestream.monkey<81>");
	if(t_fmode==String(L"a",1)){
		DBG_BLOCK();
		t_fmode=String(L"u",1);
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/filestream.monkey<82>");
	if(!t_stream->Open(t_path,t_fmode)){
		DBG_BLOCK();
		return 0;
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/filestream.monkey<83>");
	if(t_mode==String(L"a",1)){
		DBG_BLOCK();
		t_stream->Seek(t_stream->Length());
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/filestream.monkey<84>");
	return t_stream;
}
c_FileStream* c_FileStream::m_new(String t_path,String t_mode){
	DBG_ENTER("FileStream.new")
	c_FileStream *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_path,"path")
	DBG_LOCAL(t_mode,"mode")
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/filestream.monkey<31>");
	c_Stream::m_new();
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/filestream.monkey<32>");
	gc_assign(m__stream,m_OpenStream(t_path,t_mode));
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/filestream.monkey<33>");
	if(!((m__stream)!=0)){
		DBG_BLOCK();
		bbError(String(L"Failed to open stream",21));
	}
	return this;
}
c_FileStream* c_FileStream::m_new2(BBFileStream* t_stream){
	DBG_ENTER("FileStream.new")
	c_FileStream *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_stream,"stream")
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/filestream.monkey<74>");
	c_Stream::m_new();
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/filestream.monkey<75>");
	gc_assign(m__stream,t_stream);
	return this;
}
c_FileStream* c_FileStream::m_new3(){
	DBG_ENTER("FileStream.new")
	c_FileStream *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/filestream.monkey<29>");
	c_Stream::m_new();
	return this;
}
c_FileStream* c_FileStream::m_Open(String t_path,String t_mode){
	DBG_ENTER("FileStream.Open")
	DBG_LOCAL(t_path,"path")
	DBG_LOCAL(t_mode,"mode")
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/filestream.monkey<67>");
	BBFileStream* t_stream=m_OpenStream(t_path,t_mode);
	DBG_LOCAL(t_stream,"stream")
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/filestream.monkey<68>");
	if((t_stream)!=0){
		DBG_BLOCK();
		c_FileStream* t_=(new c_FileStream)->m_new2(t_stream);
		return t_;
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/filestream.monkey<69>");
	return 0;
}
void c_FileStream::p_Close(){
	DBG_ENTER("FileStream.Close")
	c_FileStream *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/filestream.monkey<37>");
	if(!((m__stream)!=0)){
		DBG_BLOCK();
		return;
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/filestream.monkey<38>");
	m__stream->Close();
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/filestream.monkey<39>");
	m__stream=0;
}
int c_FileStream::p_Read(c_DataBuffer* t_buffer,int t_offset,int t_count){
	DBG_ENTER("FileStream.Read")
	c_FileStream *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_buffer,"buffer")
	DBG_LOCAL(t_offset,"offset")
	DBG_LOCAL(t_count,"count")
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/filestream.monkey<59>");
	int t_=m__stream->Read(t_buffer,t_offset,t_count);
	return t_;
}
int c_FileStream::p_Write(c_DataBuffer* t_buffer,int t_offset,int t_count){
	DBG_ENTER("FileStream.Write")
	c_FileStream *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_buffer,"buffer")
	DBG_LOCAL(t_offset,"offset")
	DBG_LOCAL(t_count,"count")
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/filestream.monkey<63>");
	int t_=m__stream->Write(t_buffer,t_offset,t_count);
	return t_;
}
void c_FileStream::mark(){
	c_Stream::mark();
	gc_mark_q(m__stream);
}
String c_FileStream::debug(){
	String t="(FileStream)\n";
	t=c_Stream::debug()+t;
	t+=dbg_decl("_stream",&m__stream);
	return t;
}
c_List3::c_List3(){
	m__head=((new c_HeadNode3)->m_new());
}
c_List3* c_List3::m_new(){
	DBG_ENTER("List.new")
	c_List3 *self=this;
	DBG_LOCAL(self,"Self")
	return this;
}
c_Node4* c_List3::p_AddLast3(int t_data){
	DBG_ENTER("List.AddLast")
	c_List3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<108>");
	c_Node4* t_=(new c_Node4)->m_new(m__head,m__head->m__pred,t_data);
	return t_;
}
c_List3* c_List3::m_new2(Array<int > t_data){
	DBG_ENTER("List.new")
	c_List3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<13>");
	Array<int > t_=t_data;
	int t_2=0;
	while(t_2<t_.Length()){
		DBG_BLOCK();
		int t_t=t_.At(t_2);
		t_2=t_2+1;
		DBG_LOCAL(t_t,"t")
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<14>");
		p_AddLast3(t_t);
	}
	return this;
}
int c_List3::p_Compare(int t_lhs,int t_rhs){
	DBG_ENTER("List.Compare")
	c_List3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_lhs,"lhs")
	DBG_LOCAL(t_rhs,"rhs")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<32>");
	bbError(String(L"Unable to compare items",23));
	return 0;
}
int c_List3::p_Sort(int t_ascending){
	DBG_ENTER("List.Sort")
	c_List3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_ascending,"ascending")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<194>");
	int t_ccsgn=-1;
	DBG_LOCAL(t_ccsgn,"ccsgn")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<195>");
	if((t_ascending)!=0){
		DBG_BLOCK();
		t_ccsgn=1;
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<196>");
	int t_insize=1;
	DBG_LOCAL(t_insize,"insize")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<198>");
	do{
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<199>");
		int t_merges=0;
		DBG_LOCAL(t_merges,"merges")
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<200>");
		c_Node4* t_tail=m__head;
		DBG_LOCAL(t_tail,"tail")
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<201>");
		c_Node4* t_p=m__head->m__succ;
		DBG_LOCAL(t_p,"p")
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<203>");
		while(t_p!=m__head){
			DBG_BLOCK();
			DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<204>");
			t_merges+=1;
			DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<205>");
			c_Node4* t_q=t_p->m__succ;
			int t_qsize=t_insize;
			int t_psize=1;
			DBG_LOCAL(t_q,"q")
			DBG_LOCAL(t_qsize,"qsize")
			DBG_LOCAL(t_psize,"psize")
			DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<207>");
			while(t_psize<t_insize && t_q!=m__head){
				DBG_BLOCK();
				DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<208>");
				t_psize+=1;
				DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<209>");
				t_q=t_q->m__succ;
			}
			DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<212>");
			do{
				DBG_BLOCK();
				DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<213>");
				c_Node4* t_t=0;
				DBG_LOCAL(t_t,"t")
				DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<214>");
				if(((t_psize)!=0) && ((t_qsize)!=0) && t_q!=m__head){
					DBG_BLOCK();
					DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<215>");
					int t_cc=p_Compare(t_p->m__data,t_q->m__data)*t_ccsgn;
					DBG_LOCAL(t_cc,"cc")
					DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<216>");
					if(t_cc<=0){
						DBG_BLOCK();
						DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<217>");
						t_t=t_p;
						DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<218>");
						t_p=t_p->m__succ;
						DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<219>");
						t_psize-=1;
					}else{
						DBG_BLOCK();
						DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<221>");
						t_t=t_q;
						DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<222>");
						t_q=t_q->m__succ;
						DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<223>");
						t_qsize-=1;
					}
				}else{
					DBG_BLOCK();
					DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<225>");
					if((t_psize)!=0){
						DBG_BLOCK();
						DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<226>");
						t_t=t_p;
						DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<227>");
						t_p=t_p->m__succ;
						DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<228>");
						t_psize-=1;
					}else{
						DBG_BLOCK();
						DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<229>");
						if(((t_qsize)!=0) && t_q!=m__head){
							DBG_BLOCK();
							DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<230>");
							t_t=t_q;
							DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<231>");
							t_q=t_q->m__succ;
							DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<232>");
							t_qsize-=1;
						}else{
							DBG_BLOCK();
							DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<234>");
							break;
						}
					}
				}
				DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<236>");
				gc_assign(t_t->m__pred,t_tail);
				DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<237>");
				gc_assign(t_tail->m__succ,t_t);
				DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<238>");
				t_tail=t_t;
			}while(!(false));
			DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<240>");
			t_p=t_q;
		}
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<242>");
		gc_assign(t_tail->m__succ,m__head);
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<243>");
		gc_assign(m__head->m__pred,t_tail);
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<245>");
		if(t_merges<=1){
			DBG_BLOCK();
			return 0;
		}
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<247>");
		t_insize*=2;
	}while(!(false));
}
c_Enumerator2* c_List3::p_ObjectEnumerator(){
	DBG_ENTER("List.ObjectEnumerator")
	c_List3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<186>");
	c_Enumerator2* t_=(new c_Enumerator2)->m_new(this);
	return t_;
}
void c_List3::mark(){
	Object::mark();
	gc_mark_q(m__head);
}
String c_List3::debug(){
	String t="(List)\n";
	t+=dbg_decl("_head",&m__head);
	return t;
}
c_IntList::c_IntList(){
}
c_IntList* c_IntList::m_new(Array<int > t_data){
	DBG_ENTER("IntList.new")
	c_IntList *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<398>");
	c_List3::m_new2(t_data);
	return this;
}
c_IntList* c_IntList::m_new2(){
	DBG_ENTER("IntList.new")
	c_IntList *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<395>");
	c_List3::m_new();
	return this;
}
int c_IntList::p_Compare(int t_lhs,int t_rhs){
	DBG_ENTER("IntList.Compare")
	c_IntList *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_lhs,"lhs")
	DBG_LOCAL(t_rhs,"rhs")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<406>");
	int t_=t_lhs-t_rhs;
	return t_;
}
void c_IntList::mark(){
	c_List3::mark();
}
String c_IntList::debug(){
	String t="(IntList)\n";
	t=c_List3::debug()+t;
	return t;
}
c_Node4::c_Node4(){
	m__succ=0;
	m__pred=0;
	m__data=0;
}
c_Node4* c_Node4::m_new(c_Node4* t_succ,c_Node4* t_pred,int t_data){
	DBG_ENTER("Node.new")
	c_Node4 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_succ,"succ")
	DBG_LOCAL(t_pred,"pred")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<261>");
	gc_assign(m__succ,t_succ);
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<262>");
	gc_assign(m__pred,t_pred);
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<263>");
	gc_assign(m__succ->m__pred,this);
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<264>");
	gc_assign(m__pred->m__succ,this);
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<265>");
	m__data=t_data;
	return this;
}
c_Node4* c_Node4::m_new2(){
	DBG_ENTER("Node.new")
	c_Node4 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<258>");
	return this;
}
void c_Node4::mark(){
	Object::mark();
	gc_mark_q(m__succ);
	gc_mark_q(m__pred);
}
String c_Node4::debug(){
	String t="(Node)\n";
	t+=dbg_decl("_succ",&m__succ);
	t+=dbg_decl("_pred",&m__pred);
	t+=dbg_decl("_data",&m__data);
	return t;
}
c_HeadNode3::c_HeadNode3(){
}
c_HeadNode3* c_HeadNode3::m_new(){
	DBG_ENTER("HeadNode.new")
	c_HeadNode3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<310>");
	c_Node4::m_new2();
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<311>");
	gc_assign(m__succ,(this));
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<312>");
	gc_assign(m__pred,(this));
	return this;
}
void c_HeadNode3::mark(){
	c_Node4::mark();
}
String c_HeadNode3::debug(){
	String t="(HeadNode)\n";
	t=c_Node4::debug()+t;
	return t;
}
String bb_shooter_path;
c_StreamError::c_StreamError(){
	m__stream=0;
}
c_StreamError* c_StreamError::m_new(c_Stream* t_stream){
	DBG_ENTER("StreamError.new")
	c_StreamError *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_stream,"stream")
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/stream.monkey<200>");
	gc_assign(m__stream,t_stream);
	return this;
}
c_StreamError* c_StreamError::m_new2(){
	DBG_ENTER("StreamError.new")
	c_StreamError *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/stream.monkey<197>");
	return this;
}
void c_StreamError::mark(){
	ThrowableObject::mark();
	gc_mark_q(m__stream);
}
String c_StreamError::debug(){
	String t="(StreamError)\n";
	t+=dbg_decl("_stream",&m__stream);
	return t;
}
c_StreamReadError::c_StreamReadError(){
}
c_StreamReadError* c_StreamReadError::m_new(c_Stream* t_stream){
	DBG_ENTER("StreamReadError.new")
	c_StreamReadError *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_stream,"stream")
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/stream.monkey<217>");
	c_StreamError::m_new(t_stream);
	return this;
}
c_StreamReadError* c_StreamReadError::m_new2(){
	DBG_ENTER("StreamReadError.new")
	c_StreamReadError *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/stream.monkey<214>");
	c_StreamError::m_new2();
	return this;
}
void c_StreamReadError::mark(){
	c_StreamError::mark();
}
String c_StreamReadError::debug(){
	String t="(StreamReadError)\n";
	t=c_StreamError::debug()+t;
	return t;
}
c_Stack4::c_Stack4(){
	m_data=Array<c_DataBuffer* >();
	m_length=0;
}
c_Stack4* c_Stack4::m_new(){
	DBG_ENTER("Stack.new")
	c_Stack4 *self=this;
	DBG_LOCAL(self,"Self")
	return this;
}
c_Stack4* c_Stack4::m_new2(Array<c_DataBuffer* > t_data){
	DBG_ENTER("Stack.new")
	c_Stack4 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_data,"data")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<13>");
	gc_assign(this->m_data,t_data.Slice(0));
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<14>");
	this->m_length=t_data.Length();
	return this;
}
void c_Stack4::p_Push10(c_DataBuffer* t_value){
	DBG_ENTER("Stack.Push")
	c_Stack4 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_value,"value")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<71>");
	if(m_length==m_data.Length()){
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<72>");
		gc_assign(m_data,m_data.Resize(m_length*2+10));
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<74>");
	gc_assign(m_data.At(m_length),t_value);
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<75>");
	m_length+=1;
}
void c_Stack4::p_Push11(Array<c_DataBuffer* > t_values,int t_offset,int t_count){
	DBG_ENTER("Stack.Push")
	c_Stack4 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_values,"values")
	DBG_LOCAL(t_offset,"offset")
	DBG_LOCAL(t_count,"count")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<83>");
	for(int t_i=0;t_i<t_count;t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<84>");
		p_Push10(t_values.At(t_offset+t_i));
	}
}
void c_Stack4::p_Push12(Array<c_DataBuffer* > t_values,int t_offset){
	DBG_ENTER("Stack.Push")
	c_Stack4 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_values,"values")
	DBG_LOCAL(t_offset,"offset")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<79>");
	p_Push11(t_values,t_offset,t_values.Length()-t_offset);
}
c_Enumerator* c_Stack4::p_ObjectEnumerator(){
	DBG_ENTER("Stack.ObjectEnumerator")
	c_Stack4 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<188>");
	c_Enumerator* t_=(new c_Enumerator)->m_new(this);
	return t_;
}
c_DataBuffer* c_Stack4::m_NIL;
void c_Stack4::p_Length(int t_newlength){
	DBG_ENTER("Stack.Length")
	c_Stack4 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_newlength,"newlength")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<45>");
	if(t_newlength<m_length){
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<46>");
		for(int t_i=t_newlength;t_i<m_length;t_i=t_i+1){
			DBG_BLOCK();
			DBG_LOCAL(t_i,"i")
			DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<47>");
			gc_assign(m_data.At(t_i),m_NIL);
		}
	}else{
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<49>");
		if(t_newlength>m_data.Length()){
			DBG_BLOCK();
			DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<50>");
			gc_assign(m_data,m_data.Resize(bb_math_Max(m_length*2+10,t_newlength)));
		}
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<52>");
	m_length=t_newlength;
}
int c_Stack4::p_Length2(){
	DBG_ENTER("Stack.Length")
	c_Stack4 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<56>");
	return m_length;
}
void c_Stack4::mark(){
	Object::mark();
	gc_mark_q(m_data);
}
String c_Stack4::debug(){
	String t="(Stack)\n";
	t+=dbg_decl("NIL",&c_Stack4::m_NIL);
	t+=dbg_decl("data",&m_data);
	t+=dbg_decl("length",&m_length);
	return t;
}
c_Enumerator::c_Enumerator(){
	m_stack=0;
	m_index=0;
}
c_Enumerator* c_Enumerator::m_new(c_Stack4* t_stack){
	DBG_ENTER("Enumerator.new")
	c_Enumerator *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_stack,"stack")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<259>");
	gc_assign(this->m_stack,t_stack);
	return this;
}
c_Enumerator* c_Enumerator::m_new2(){
	DBG_ENTER("Enumerator.new")
	c_Enumerator *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<256>");
	return this;
}
bool c_Enumerator::p_HasNext(){
	DBG_ENTER("Enumerator.HasNext")
	c_Enumerator *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<263>");
	bool t_=m_index<m_stack->p_Length2();
	return t_;
}
c_DataBuffer* c_Enumerator::p_NextObject(){
	DBG_ENTER("Enumerator.NextObject")
	c_Enumerator *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<267>");
	m_index+=1;
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/stack.monkey<268>");
	c_DataBuffer* t_=m_stack->m_data.At(m_index-1);
	return t_;
}
void c_Enumerator::mark(){
	Object::mark();
	gc_mark_q(m_stack);
}
String c_Enumerator::debug(){
	String t="(Enumerator)\n";
	t+=dbg_decl("stack",&m_stack);
	t+=dbg_decl("index",&m_index);
	return t;
}
c_Enumerator2::c_Enumerator2(){
	m__list=0;
	m__curr=0;
}
c_Enumerator2* c_Enumerator2::m_new(c_List3* t_list){
	DBG_ENTER("Enumerator.new")
	c_Enumerator2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_list,"list")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<326>");
	gc_assign(m__list,t_list);
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<327>");
	gc_assign(m__curr,t_list->m__head->m__succ);
	return this;
}
c_Enumerator2* c_Enumerator2::m_new2(){
	DBG_ENTER("Enumerator.new")
	c_Enumerator2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<323>");
	return this;
}
bool c_Enumerator2::p_HasNext(){
	DBG_ENTER("Enumerator.HasNext")
	c_Enumerator2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<331>");
	while(m__curr->m__succ->m__pred!=m__curr){
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<332>");
		gc_assign(m__curr,m__curr->m__succ);
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<334>");
	bool t_=m__curr!=m__list->m__head;
	return t_;
}
int c_Enumerator2::p_NextObject(){
	DBG_ENTER("Enumerator.NextObject")
	c_Enumerator2 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<338>");
	int t_data=m__curr->m__data;
	DBG_LOCAL(t_data,"data")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<339>");
	gc_assign(m__curr,m__curr->m__succ);
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<340>");
	return t_data;
}
void c_Enumerator2::mark(){
	Object::mark();
	gc_mark_q(m__list);
	gc_mark_q(m__curr);
}
String c_Enumerator2::debug(){
	String t="(Enumerator)\n";
	t+=dbg_decl("_list",&m__list);
	t+=dbg_decl("_curr",&m__curr);
	return t;
}
Array<int > bb_shooter_playerscores;
Array<String > bb_shooter_names;
void bb_shooter_InsertionSort(){
	DBG_ENTER("InsertionSort")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1095>");
	for(int t_index=1;t_index<bb_shooter_playerscores.Length();t_index=t_index+1){
		DBG_BLOCK();
		DBG_LOCAL(t_index,"index")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1096>");
		int t_value=bb_shooter_playerscores.At(t_index);
		DBG_LOCAL(t_value,"value")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1097>");
		String t_item=bb_shooter_names.At(t_index);
		DBG_LOCAL(t_item,"item")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1098>");
		int t_previous=t_index-1;
		DBG_LOCAL(t_previous,"previous")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1101>");
		while(t_previous>=0 && bb_shooter_playerscores.At(t_previous)<t_value){
			DBG_BLOCK();
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1102>");
			bb_shooter_playerscores.At(t_previous+1)=bb_shooter_playerscores.At(t_previous);
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1103>");
			bb_shooter_names.At(t_previous+1)=bb_shooter_names.At(t_previous);
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1104>");
			t_previous-=1;
		}
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1106>");
		bb_shooter_playerscores.At(t_previous+1)=t_value;
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1107>");
		bb_shooter_names.At(t_previous+1)=t_item;
	}
}
int bb_shooter_toptenplayers(){
	DBG_ENTER("toptenplayers")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1039>");
	c_FileStream* t_scores_file=0;
	DBG_LOCAL(t_scores_file,"scores_file")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1040>");
	String t_scores_data=String();
	DBG_LOCAL(t_scores_data,"scores_data")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1041>");
	int t_scoresInt=0;
	DBG_LOCAL(t_scoresInt,"scoresInt")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1042>");
	c_IntList* t_highscoreList=(new c_IntList)->m_new2();
	DBG_LOCAL(t_highscoreList,"highscoreList")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1043>");
	int t_counter=0;
	DBG_LOCAL(t_counter,"counter")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1044>");
	int t_last=0;
	DBG_LOCAL(t_last,"last")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1045>");
	int t_itemcount=0;
	DBG_LOCAL(t_itemcount,"itemcount")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1047>");
	t_scores_file=c_FileStream::m_Open(bb_shooter_path,String(L"r",1));
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1048>");
	t_scores_data=t_scores_file->p_ReadString3(String(L"utf8",4));
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1049>");
	t_scores_file->p_Close();
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1051>");
	Array<String > t_=t_scores_data.Split(String(L"\n",1));
	int t_2=0;
	while(t_2<t_.Length()){
		DBG_BLOCK();
		String t_eachscore=t_.At(t_2);
		t_2=t_2+1;
		DBG_LOCAL(t_eachscore,"eachscore")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1052>");
		t_scoresInt=(t_eachscore).ToInt();
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1053>");
		t_highscoreList->p_AddLast3(t_scoresInt);
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1055>");
	t_highscoreList->p_Sort(0);
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1057>");
	t_counter=0;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1058>");
	c_Enumerator2* t_3=t_highscoreList->p_ObjectEnumerator();
	while(t_3->p_HasNext()){
		DBG_BLOCK();
		int t_smallest=t_3->p_NextObject();
		DBG_LOCAL(t_smallest,"smallest")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1059>");
		if(t_counter==10){
			DBG_BLOCK();
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1060>");
			t_last=t_smallest;
		}
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1062>");
		t_counter+=1;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1066>");
	for(int t_pointer=0;t_pointer<10;t_pointer=t_pointer+1){
		DBG_BLOCK();
		DBG_LOCAL(t_pointer,"pointer")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1067>");
		bb_shooter_playerscores.At(t_pointer)=0;
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1068>");
		bb_shooter_names.At(t_pointer)=String();
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1070>");
	t_counter=0;
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1072>");
	Array<String > t_4=t_scores_data.Split(String(L"\n",1));
	int t_5=0;
	while(t_5<t_4.Length()){
		DBG_BLOCK();
		String t_score=t_4.At(t_5);
		t_5=t_5+1;
		DBG_LOCAL(t_score,"score")
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1073>");
		t_itemcount=0;
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1074>");
		Array<String > t_6=t_score.Split(String(L",",1));
		int t_7=0;
		while(t_7<t_6.Length()){
			DBG_BLOCK();
			String t_item=t_6.At(t_7);
			t_7=t_7+1;
			DBG_LOCAL(t_item,"item")
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1076>");
			if(t_itemcount==0 && (t_item).ToInt()>=t_last){
				DBG_BLOCK();
				DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1077>");
				bb_shooter_playerscores.At(t_counter)=(t_item).ToInt();
			}
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1079>");
			if(t_itemcount==1 && bb_shooter_playerscores.At(t_counter)>=t_last){
				DBG_BLOCK();
				DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1080>");
				bb_shooter_names.At(t_counter)=t_item;
			}
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1082>");
			t_itemcount+=1;
		}
		DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1084>");
		if(bb_shooter_playerscores.At(t_counter)>=t_last && t_counter<10){
			DBG_BLOCK();
			DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1085>");
			t_counter+=1;
		}
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1088>");
	bb_shooter_InsertionSort();
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1089>");
	return 1;
}
int bb_audio_SetMusicVolume(Float t_volume){
	DBG_ENTER("SetMusicVolume")
	DBG_LOCAL(t_volume,"volume")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/audio.monkey<105>");
	bb_audio_device->SetMusicVolume(t_volume);
	return 0;
}
int bb_input_GetChar(){
	DBG_ENTER("GetChar")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/input.monkey<48>");
	int t_=bb_input_device->p_GetChar();
	return t_;
}
c_StreamWriteError::c_StreamWriteError(){
}
c_StreamWriteError* c_StreamWriteError::m_new(c_Stream* t_stream){
	DBG_ENTER("StreamWriteError.new")
	c_StreamWriteError *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_stream,"stream")
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/stream.monkey<229>");
	c_StreamError::m_new(t_stream);
	return this;
}
c_StreamWriteError* c_StreamWriteError::m_new2(){
	DBG_ENTER("StreamWriteError.new")
	c_StreamWriteError *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/brl/stream.monkey<226>");
	c_StreamError::m_new2();
	return this;
}
void c_StreamWriteError::mark(){
	c_StreamError::mark();
}
String c_StreamWriteError::debug(){
	String t="(StreamWriteError)\n";
	t=c_StreamError::debug()+t;
	return t;
}
int bb_shooter_writenamesandscore(String t_name,int t_score){
	DBG_ENTER("writenamesandscore")
	DBG_LOCAL(t_name,"name")
	DBG_LOCAL(t_score,"score")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1023>");
	c_FileStream* t_score_file=0;
	DBG_LOCAL(t_score_file,"score_file")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1024>");
	String t_score_data=String();
	DBG_LOCAL(t_score_data,"score_data")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1026>");
	t_score_file=c_FileStream::m_Open(bb_shooter_path,String(L"a",1));
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1027>");
	t_score_data=String(t_score);
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1028>");
	t_score_file->p_WriteString(t_score_data+String(L",",1)+t_name+String(L"\r\n",2),String(L"utf8",4));
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1029>");
	t_score_file->p_Close();
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1030>");
	return 1;
}
int bb_audio_StopMusic(){
	DBG_ENTER("StopMusic")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/audio.monkey<89>");
	bb_audio_device->StopMusic();
	return 0;
}
int bb_audio_PlaySound(c_Sound* t_sound,int t_channel,int t_flags){
	DBG_ENTER("PlaySound")
	DBG_LOCAL(t_sound,"sound")
	DBG_LOCAL(t_channel,"channel")
	DBG_LOCAL(t_flags,"flags")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/audio.monkey<53>");
	if(((t_sound)!=0) && ((t_sound->m_sample)!=0)){
		DBG_BLOCK();
		bb_audio_device->PlaySample(t_sound->m_sample,t_channel,t_flags);
	}
	return 0;
}
int bb_input_KeyDown(int t_key){
	DBG_ENTER("KeyDown")
	DBG_LOCAL(t_key,"key")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/input.monkey<40>");
	int t_=((bb_input_device->p_KeyDown(t_key))?1:0);
	return t_;
}
c_Enumerator3::c_Enumerator3(){
	m__list=0;
	m__curr=0;
}
c_Enumerator3* c_Enumerator3::m_new(c_List* t_list){
	DBG_ENTER("Enumerator.new")
	c_Enumerator3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_list,"list")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<326>");
	gc_assign(m__list,t_list);
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<327>");
	gc_assign(m__curr,t_list->m__head->m__succ);
	return this;
}
c_Enumerator3* c_Enumerator3::m_new2(){
	DBG_ENTER("Enumerator.new")
	c_Enumerator3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<323>");
	return this;
}
bool c_Enumerator3::p_HasNext(){
	DBG_ENTER("Enumerator.HasNext")
	c_Enumerator3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<331>");
	while(m__curr->m__succ->m__pred!=m__curr){
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<332>");
		gc_assign(m__curr,m__curr->m__succ);
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<334>");
	bool t_=m__curr!=m__list->m__head;
	return t_;
}
c_RayGunBullet* c_Enumerator3::p_NextObject(){
	DBG_ENTER("Enumerator.NextObject")
	c_Enumerator3 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<338>");
	c_RayGunBullet* t_data=m__curr->m__data;
	DBG_LOCAL(t_data,"data")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<339>");
	gc_assign(m__curr,m__curr->m__succ);
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<340>");
	return t_data;
}
void c_Enumerator3::mark(){
	Object::mark();
	gc_mark_q(m__list);
	gc_mark_q(m__curr);
}
String c_Enumerator3::debug(){
	String t="(Enumerator)\n";
	t+=dbg_decl("_list",&m__list);
	t+=dbg_decl("_curr",&m__curr);
	return t;
}
c_Enumerator4::c_Enumerator4(){
	m__list=0;
	m__curr=0;
}
c_Enumerator4* c_Enumerator4::m_new(c_List2* t_list){
	DBG_ENTER("Enumerator.new")
	c_Enumerator4 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_LOCAL(t_list,"list")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<326>");
	gc_assign(m__list,t_list);
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<327>");
	gc_assign(m__curr,t_list->m__head->m__succ);
	return this;
}
c_Enumerator4* c_Enumerator4::m_new2(){
	DBG_ENTER("Enumerator.new")
	c_Enumerator4 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<323>");
	return this;
}
bool c_Enumerator4::p_HasNext(){
	DBG_ENTER("Enumerator.HasNext")
	c_Enumerator4 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<331>");
	while(m__curr->m__succ->m__pred!=m__curr){
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<332>");
		gc_assign(m__curr,m__curr->m__succ);
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<334>");
	bool t_=m__curr!=m__list->m__head;
	return t_;
}
c_Zombie* c_Enumerator4::p_NextObject(){
	DBG_ENTER("Enumerator.NextObject")
	c_Enumerator4 *self=this;
	DBG_LOCAL(self,"Self")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<338>");
	c_Zombie* t_data=m__curr->m__data;
	DBG_LOCAL(t_data,"data")
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<339>");
	gc_assign(m__curr,m__curr->m__succ);
	DBG_INFO("D:/MonkeyXFree84f/modules/monkey/list.monkey<340>");
	return t_data;
}
void c_Enumerator4::mark(){
	Object::mark();
	gc_mark_q(m__list);
	gc_mark_q(m__curr);
}
String c_Enumerator4::debug(){
	String t="(Enumerator)\n";
	t+=dbg_decl("_list",&m__list);
	t+=dbg_decl("_curr",&m__curr);
	return t;
}
bool bb_shooter_intersects(int t_x1,int t_y1,int t_w1,int t_h1,int t_x2,int t_y2,int t_w2,int t_h2){
	DBG_ENTER("intersects")
	DBG_LOCAL(t_x1,"x1")
	DBG_LOCAL(t_y1,"y1")
	DBG_LOCAL(t_w1,"w1")
	DBG_LOCAL(t_h1,"h1")
	DBG_LOCAL(t_x2,"x2")
	DBG_LOCAL(t_y2,"y2")
	DBG_LOCAL(t_w2,"w2")
	DBG_LOCAL(t_h2,"h2")
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1014>");
	if(t_x1>=t_x2+t_w2 || t_x1+t_w1<=t_x2){
		DBG_BLOCK();
		return false;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1015>");
	if(t_y1>=t_y2+t_h2 || t_y1+t_h1<=t_y2){
		DBG_BLOCK();
		return false;
	}
	DBG_INFO("E:/1P game forked and ended/Z-WAVE 1 player/Game files/shooter.monkey<1016>");
	return true;
}
int bb_graphics_DebugRenderDevice(){
	DBG_ENTER("DebugRenderDevice")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<53>");
	if(!((bb_graphics_renderDevice)!=0)){
		DBG_BLOCK();
		bbError(String(L"Rendering operations can only be performed inside OnRender",58));
	}
	return 0;
}
int bb_graphics_DrawImage(c_Image* t_image,Float t_x,Float t_y,int t_frame){
	DBG_ENTER("DrawImage")
	DBG_LOCAL(t_image,"image")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_frame,"frame")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<452>");
	bb_graphics_DebugRenderDevice();
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<453>");
	if(t_frame<0 || t_frame>=t_image->m_frames.Length()){
		DBG_BLOCK();
		bbError(String(L"Invalid image frame",19));
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<456>");
	c_Frame* t_f=t_image->m_frames.At(t_frame);
	DBG_LOCAL(t_f,"f")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<458>");
	bb_graphics_context->p_Validate();
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<460>");
	if((t_image->m_flags&65536)!=0){
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<461>");
		bb_graphics_renderDevice->DrawSurface(t_image->m_surface,t_x-t_image->m_tx,t_y-t_image->m_ty);
	}else{
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<463>");
		bb_graphics_renderDevice->DrawSurface2(t_image->m_surface,t_x-t_image->m_tx,t_y-t_image->m_ty,t_f->m_x,t_f->m_y,t_image->m_width,t_image->m_height);
	}
	return 0;
}
int bb_graphics_PushMatrix(){
	DBG_ENTER("PushMatrix")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<333>");
	int t_sp=bb_graphics_context->m_matrixSp;
	DBG_LOCAL(t_sp,"sp")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<334>");
	if(t_sp==bb_graphics_context->m_matrixStack.Length()){
		DBG_BLOCK();
		gc_assign(bb_graphics_context->m_matrixStack,bb_graphics_context->m_matrixStack.Resize(t_sp*2));
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<335>");
	bb_graphics_context->m_matrixStack.At(t_sp+0)=bb_graphics_context->m_ix;
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<336>");
	bb_graphics_context->m_matrixStack.At(t_sp+1)=bb_graphics_context->m_iy;
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<337>");
	bb_graphics_context->m_matrixStack.At(t_sp+2)=bb_graphics_context->m_jx;
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<338>");
	bb_graphics_context->m_matrixStack.At(t_sp+3)=bb_graphics_context->m_jy;
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<339>");
	bb_graphics_context->m_matrixStack.At(t_sp+4)=bb_graphics_context->m_tx;
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<340>");
	bb_graphics_context->m_matrixStack.At(t_sp+5)=bb_graphics_context->m_ty;
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<341>");
	bb_graphics_context->m_matrixSp=t_sp+6;
	return 0;
}
int bb_graphics_Transform(Float t_ix,Float t_iy,Float t_jx,Float t_jy,Float t_tx,Float t_ty){
	DBG_ENTER("Transform")
	DBG_LOCAL(t_ix,"ix")
	DBG_LOCAL(t_iy,"iy")
	DBG_LOCAL(t_jx,"jx")
	DBG_LOCAL(t_jy,"jy")
	DBG_LOCAL(t_tx,"tx")
	DBG_LOCAL(t_ty,"ty")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<355>");
	Float t_ix2=t_ix*bb_graphics_context->m_ix+t_iy*bb_graphics_context->m_jx;
	DBG_LOCAL(t_ix2,"ix2")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<356>");
	Float t_iy2=t_ix*bb_graphics_context->m_iy+t_iy*bb_graphics_context->m_jy;
	DBG_LOCAL(t_iy2,"iy2")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<357>");
	Float t_jx2=t_jx*bb_graphics_context->m_ix+t_jy*bb_graphics_context->m_jx;
	DBG_LOCAL(t_jx2,"jx2")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<358>");
	Float t_jy2=t_jx*bb_graphics_context->m_iy+t_jy*bb_graphics_context->m_jy;
	DBG_LOCAL(t_jy2,"jy2")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<359>");
	Float t_tx2=t_tx*bb_graphics_context->m_ix+t_ty*bb_graphics_context->m_jx+bb_graphics_context->m_tx;
	DBG_LOCAL(t_tx2,"tx2")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<360>");
	Float t_ty2=t_tx*bb_graphics_context->m_iy+t_ty*bb_graphics_context->m_jy+bb_graphics_context->m_ty;
	DBG_LOCAL(t_ty2,"ty2")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<361>");
	bb_graphics_SetMatrix(t_ix2,t_iy2,t_jx2,t_jy2,t_tx2,t_ty2);
	return 0;
}
int bb_graphics_Transform2(Array<Float > t_m){
	DBG_ENTER("Transform")
	DBG_LOCAL(t_m,"m")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<351>");
	bb_graphics_Transform(t_m.At(0),t_m.At(1),t_m.At(2),t_m.At(3),t_m.At(4),t_m.At(5));
	return 0;
}
int bb_graphics_Translate(Float t_x,Float t_y){
	DBG_ENTER("Translate")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<365>");
	bb_graphics_Transform(FLOAT(1.0),FLOAT(0.0),FLOAT(0.0),FLOAT(1.0),t_x,t_y);
	return 0;
}
int bb_graphics_Rotate(Float t_angle){
	DBG_ENTER("Rotate")
	DBG_LOCAL(t_angle,"angle")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<373>");
	bb_graphics_Transform((Float)cos((t_angle)*D2R),-(Float)sin((t_angle)*D2R),(Float)sin((t_angle)*D2R),(Float)cos((t_angle)*D2R),FLOAT(0.0),FLOAT(0.0));
	return 0;
}
int bb_graphics_Scale(Float t_x,Float t_y){
	DBG_ENTER("Scale")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<369>");
	bb_graphics_Transform(t_x,FLOAT(0.0),FLOAT(0.0),t_y,FLOAT(0.0),FLOAT(0.0));
	return 0;
}
int bb_graphics_PopMatrix(){
	DBG_ENTER("PopMatrix")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<345>");
	int t_sp=bb_graphics_context->m_matrixSp-6;
	DBG_LOCAL(t_sp,"sp")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<346>");
	bb_graphics_SetMatrix(bb_graphics_context->m_matrixStack.At(t_sp+0),bb_graphics_context->m_matrixStack.At(t_sp+1),bb_graphics_context->m_matrixStack.At(t_sp+2),bb_graphics_context->m_matrixStack.At(t_sp+3),bb_graphics_context->m_matrixStack.At(t_sp+4),bb_graphics_context->m_matrixStack.At(t_sp+5));
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<347>");
	bb_graphics_context->m_matrixSp=t_sp;
	return 0;
}
int bb_graphics_DrawImage2(c_Image* t_image,Float t_x,Float t_y,Float t_rotation,Float t_scaleX,Float t_scaleY,int t_frame){
	DBG_ENTER("DrawImage")
	DBG_LOCAL(t_image,"image")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_rotation,"rotation")
	DBG_LOCAL(t_scaleX,"scaleX")
	DBG_LOCAL(t_scaleY,"scaleY")
	DBG_LOCAL(t_frame,"frame")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<470>");
	bb_graphics_DebugRenderDevice();
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<471>");
	if(t_frame<0 || t_frame>=t_image->m_frames.Length()){
		DBG_BLOCK();
		bbError(String(L"Invalid image frame",19));
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<474>");
	c_Frame* t_f=t_image->m_frames.At(t_frame);
	DBG_LOCAL(t_f,"f")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<476>");
	bb_graphics_PushMatrix();
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<478>");
	bb_graphics_Translate(t_x,t_y);
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<479>");
	bb_graphics_Rotate(t_rotation);
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<480>");
	bb_graphics_Scale(t_scaleX,t_scaleY);
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<482>");
	bb_graphics_Translate(-t_image->m_tx,-t_image->m_ty);
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<484>");
	bb_graphics_context->p_Validate();
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<486>");
	if((t_image->m_flags&65536)!=0){
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<487>");
		bb_graphics_renderDevice->DrawSurface(t_image->m_surface,FLOAT(0.0),FLOAT(0.0));
	}else{
		DBG_BLOCK();
		DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<489>");
		bb_graphics_renderDevice->DrawSurface2(t_image->m_surface,FLOAT(0.0),FLOAT(0.0),t_f->m_x,t_f->m_y,t_image->m_width,t_image->m_height);
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<492>");
	bb_graphics_PopMatrix();
	return 0;
}
int bb_graphics_Cls(Float t_r,Float t_g,Float t_b){
	DBG_ENTER("Cls")
	DBG_LOCAL(t_r,"r")
	DBG_LOCAL(t_g,"g")
	DBG_LOCAL(t_b,"b")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<378>");
	bb_graphics_DebugRenderDevice();
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<380>");
	bb_graphics_renderDevice->Cls(t_r,t_g,t_b);
	return 0;
}
Array<Float > bb_graphics_GetColor(){
	DBG_ENTER("GetColor")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<261>");
	Float t_2[]={bb_graphics_context->m_color_r,bb_graphics_context->m_color_g,bb_graphics_context->m_color_b};
	Array<Float > t_=Array<Float >(t_2,3);
	return t_;
}
int bb_graphics_GetColor2(Array<Float > t_color){
	DBG_ENTER("GetColor")
	DBG_LOCAL(t_color,"color")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<265>");
	t_color.At(0)=bb_graphics_context->m_color_r;
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<266>");
	t_color.At(1)=bb_graphics_context->m_color_g;
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<267>");
	t_color.At(2)=bb_graphics_context->m_color_b;
	return 0;
}
c_Image* bb_graphics_CreateImage(int t_width,int t_height,int t_frameCount,int t_flags){
	DBG_ENTER("CreateImage")
	DBG_LOCAL(t_width,"width")
	DBG_LOCAL(t_height,"height")
	DBG_LOCAL(t_frameCount,"frameCount")
	DBG_LOCAL(t_flags,"flags")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<249>");
	gxtkSurface* t_surf=bb_graphics_device->CreateSurface(t_width*t_frameCount,t_height);
	DBG_LOCAL(t_surf,"surf")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<250>");
	if((t_surf)!=0){
		DBG_BLOCK();
		c_Image* t_=((new c_Image)->m_new())->p_Init(t_surf,t_frameCount,t_flags);
		return t_;
	}
	return 0;
}
int bb_graphics_ReadPixels(Array<int > t_pixels,int t_x,int t_y,int t_width,int t_height,int t_offset,int t_pitch){
	DBG_ENTER("ReadPixels")
	DBG_LOCAL(t_pixels,"pixels")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_width,"width")
	DBG_LOCAL(t_height,"height")
	DBG_LOCAL(t_offset,"offset")
	DBG_LOCAL(t_pitch,"pitch")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<536>");
	if(!((t_pitch)!=0)){
		DBG_BLOCK();
		t_pitch=t_width;
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<539>");
	bb_graphics_DebugRenderDevice();
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<540>");
	if(t_x<0 || t_y<0 || t_x+t_width>bb_app_DeviceWidth() || t_y+t_height>bb_app_DeviceHeight()){
		DBG_BLOCK();
		bbError(String(L"Invalid pixel rectangle",23));
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<541>");
	if(t_offset<0 || t_pitch<0 || t_offset+(t_height-1)*t_pitch+t_width>t_pixels.Length()){
		DBG_BLOCK();
		bbError(String(L"Invalid array rectangle",23));
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<544>");
	bb_graphics_renderDevice->ReadPixels(t_pixels,t_x,t_y,t_width,t_height,t_offset,t_pitch);
	return 0;
}
int bb_graphics_DrawRect(Float t_x,Float t_y,Float t_w,Float t_h){
	DBG_ENTER("DrawRect")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_w,"w")
	DBG_LOCAL(t_h,"h")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<393>");
	bb_graphics_DebugRenderDevice();
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<395>");
	bb_graphics_context->p_Validate();
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<396>");
	bb_graphics_renderDevice->DrawRect(t_x,t_y,t_w,t_h);
	return 0;
}
int bb_graphics_DrawPoly(Array<Float > t_verts){
	DBG_ENTER("DrawPoly")
	DBG_LOCAL(t_verts,"verts")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<433>");
	bb_graphics_DebugRenderDevice();
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<435>");
	bb_graphics_context->p_Validate();
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<436>");
	bb_graphics_renderDevice->DrawPoly(t_verts);
	return 0;
}
int bb_graphics_DrawPoly2(Array<Float > t_verts,c_Image* t_image,int t_frame){
	DBG_ENTER("DrawPoly")
	DBG_LOCAL(t_verts,"verts")
	DBG_LOCAL(t_image,"image")
	DBG_LOCAL(t_frame,"frame")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<441>");
	bb_graphics_DebugRenderDevice();
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<442>");
	if(t_frame<0 || t_frame>=t_image->m_frames.Length()){
		DBG_BLOCK();
		bbError(String(L"Invalid image frame",19));
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<444>");
	c_Frame* t_f=t_image->m_frames.At(t_frame);
	DBG_LOCAL(t_f,"f")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<445>");
	bb_graphics_context->p_Validate();
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<446>");
	bb_graphics_renderDevice->DrawPoly2(t_verts,t_image->m_surface,t_f->m_x,t_f->m_y);
	return 0;
}
int bb_graphics_DrawText(String t_text,Float t_x,Float t_y,Float t_xalign,Float t_yalign){
	DBG_ENTER("DrawText")
	DBG_LOCAL(t_text,"text")
	DBG_LOCAL(t_x,"x")
	DBG_LOCAL(t_y,"y")
	DBG_LOCAL(t_xalign,"xalign")
	DBG_LOCAL(t_yalign,"yalign")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<577>");
	bb_graphics_DebugRenderDevice();
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<579>");
	if(!((bb_graphics_context->m_font)!=0)){
		DBG_BLOCK();
		return 0;
	}
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<581>");
	int t_w=bb_graphics_context->m_font->p_Width();
	DBG_LOCAL(t_w,"w")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<582>");
	int t_h=bb_graphics_context->m_font->p_Height();
	DBG_LOCAL(t_h,"h")
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<584>");
	t_x-=(Float)floor(Float(t_w*t_text.Length())*t_xalign);
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<585>");
	t_y-=(Float)floor(Float(t_h)*t_yalign);
	DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<587>");
	for(int t_i=0;t_i<t_text.Length();t_i=t_i+1){
		DBG_BLOCK();
		DBG_LOCAL(t_i,"i")
		DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<588>");
		int t_ch=(int)t_text.At(t_i)-bb_graphics_context->m_firstChar;
		DBG_LOCAL(t_ch,"ch")
		DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<589>");
		if(t_ch>=0 && t_ch<bb_graphics_context->m_font->p_Frames()){
			DBG_BLOCK();
			DBG_INFO("D:/MonkeyXFree84f/modules/mojo/graphics.monkey<590>");
			bb_graphics_DrawImage(bb_graphics_context->m_font,t_x+Float(t_i*t_w),t_y,t_ch);
		}
	}
	return 0;
}
int bbInit(){
	GC_CTOR
	bb_app__app=0;
	DBG_GLOBAL("_app",&bb_app__app);
	bb_app__delegate=0;
	DBG_GLOBAL("_delegate",&bb_app__delegate);
	bb_app__game=BBGame::Game();
	DBG_GLOBAL("_game",&bb_app__game);
	bb_shooter_Game=0;
	DBG_GLOBAL("Game",&bb_shooter_Game);
	bb_graphics_device=0;
	DBG_GLOBAL("device",&bb_graphics_device);
	bb_graphics_context=(new c_GraphicsContext)->m_new();
	DBG_GLOBAL("context",&bb_graphics_context);
	c_Image::m_DefaultFlags=0;
	DBG_GLOBAL("DefaultFlags",&c_Image::m_DefaultFlags);
	bb_audio_device=0;
	DBG_GLOBAL("device",&bb_audio_device);
	bb_input_device=0;
	DBG_GLOBAL("device",&bb_input_device);
	bb_app__devWidth=0;
	DBG_GLOBAL("_devWidth",&bb_app__devWidth);
	bb_app__devHeight=0;
	DBG_GLOBAL("_devHeight",&bb_app__devHeight);
	bb_app__displayModes=Array<c_DisplayMode* >();
	DBG_GLOBAL("_displayModes",&bb_app__displayModes);
	bb_app__desktopMode=0;
	DBG_GLOBAL("_desktopMode",&bb_app__desktopMode);
	bb_graphics_renderDevice=0;
	DBG_GLOBAL("renderDevice",&bb_graphics_renderDevice);
	bb_random_Seed=1234;
	DBG_GLOBAL("Seed",&bb_random_Seed);
	bb_app__updateRate=0;
	DBG_GLOBAL("_updateRate",&bb_app__updateRate);
	c_Game_app::m_GameState=String();
	DBG_GLOBAL("GameState",&c_Game_app::m_GameState);
	c_TFont_Poly::m_points=(new c_FloatStack)->m_new2();
	DBG_GLOBAL("points",&c_TFont_Poly::m_points);
	c_Stack2::m_NIL=0;
	DBG_GLOBAL("NIL",&c_Stack2::m_NIL);
	bb_shooter_lbfont=0;
	DBG_GLOBAL("lbfont",&bb_shooter_lbfont);
	bb_shooter_path=String(L"C:\\Users\\Yusuf\\Work\\Z-WAVE - Coursework final version\\Game files\\shooter.data\\scorefile.txt",91);
	DBG_GLOBAL("path",&bb_shooter_path);
	c_Stack4::m_NIL=0;
	DBG_GLOBAL("NIL",&c_Stack4::m_NIL);
	bb_shooter_playerscores=Array<int >(11);
	DBG_GLOBAL("playerscores",&bb_shooter_playerscores);
	bb_shooter_names=Array<String >(11);
	DBG_GLOBAL("names",&bb_shooter_names);
	c_Stack3::m_NIL=Array<Float >();
	DBG_GLOBAL("NIL",&c_Stack3::m_NIL);
	return 0;
}
void gc_mark(){
	gc_mark_q(bb_app__app);
	gc_mark_q(bb_app__delegate);
	gc_mark_q(bb_shooter_Game);
	gc_mark_q(bb_graphics_device);
	gc_mark_q(bb_graphics_context);
	gc_mark_q(bb_audio_device);
	gc_mark_q(bb_input_device);
	gc_mark_q(bb_app__displayModes);
	gc_mark_q(bb_app__desktopMode);
	gc_mark_q(bb_graphics_renderDevice);
	gc_mark_q(c_TFont_Poly::m_points);
	gc_mark_q(bb_shooter_lbfont);
	gc_mark_q(c_Stack4::m_NIL);
	gc_mark_q(bb_shooter_playerscores);
	gc_mark_q(bb_shooter_names);
	gc_mark_q(c_Stack3::m_NIL);
}
//${TRANSCODE_END}

int main( int argc,const char *argv[] ){

	BBMonkeyGame::Main( argc,argv );
}
