// @cesar This is going to be a dummy exception and we use it as an example on
// how to add a new class

package java.lang;

public class DummyException extends Exception {

	// this is the cause
	private DummyCause cause = null;

	// this one is going to have an extra field
	public static enum DummyCause {
		SQL_ERROR,
		NETWORK_ERROR,
		DISK_ERROR,
		CPU_ERROR,
		SOMETHING_ELSE
	}	

	// and  a simple constructor
	public DummyException(String message, DummyCause cause){
		super(message);
		this.cause = cause;
	}

	public DummyCause getDummyCause(){
		return this.cause;
	}

}