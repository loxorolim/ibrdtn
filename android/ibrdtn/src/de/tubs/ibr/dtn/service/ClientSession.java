/*
 * ClientSession.java
 * 
 * Copyright (C) 2011 IBR, TU Braunschweig
 *
 * Written-by: Johannes Morgenroth <morgenroth@ibr.cs.tu-bs.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
package de.tubs.ibr.dtn.service;

import ibrdtn.api.APIConnection;
import ibrdtn.api.ExtendedClient.APIException;
import ibrdtn.api.object.Bundle;
import ibrdtn.api.object.EID;
import ibrdtn.api.object.InputStreamBlockData;
import ibrdtn.api.object.PayloadBlock;
import ibrdtn.api.object.PlainSerializer;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;

import android.content.Context;
import android.content.Intent;
import android.os.ParcelFileDescriptor;
import android.os.RemoteException;
import android.util.Log;
import de.tubs.ibr.dtn.api.BundleID;
import de.tubs.ibr.dtn.api.DTNSession;
import de.tubs.ibr.dtn.api.DTNSessionCallback;
import de.tubs.ibr.dtn.api.GroupEndpoint;
import de.tubs.ibr.dtn.api.Registration;
import de.tubs.ibr.dtn.api.SessionDestroyedException;
import de.tubs.ibr.dtn.api.SingletonEndpoint;

public class ClientSession {
	
	private final static String TAG = "ClientSession";
	
	private String _package_name = null;

	private Context context = null;
    private APISession _session = null;
    private Registration _registration = null;
    
    private Boolean _daemon_online = false;
    
	public ClientSession(Context context, Registration reg, String packageName)
	{
		// create a unique session key
		this.context = context;
		this._package_name = packageName;
		this._registration = reg;
	}
	
	public native void setEndpoint(String id);

	public native void addRegistration(String eid);

	public native void loadBundle(String id);

	public native void getBundle();

	public native void loadAndGetBundle();

	public native void markDelivered(String bundleId);

	public native void sendBundle(byte[] output);

	/**
	 * Send a bundle directly to the daemon.
	 * 
	 * @param bundle
	 *            The bundle to send.
	 * @throws APIException
	 *             If the transmission fails.
	 */
	public synchronized void send(Bundle bundle)
	{
		// TODO: implement native

		ByteArrayOutputStream out = new ByteArrayOutputStream();

		// // clear the previous bundle first
		// if (query("bundle clear") != 200) throw new
		// APIException("bundle clear failed");
		//
		// // announce a proceeding plain bundle
		// if (query("bundle put plain") != 100) throw new
		// APIException("bundle put failed");

		PlainSerializer serializer = new PlainSerializer(out);

		try
		{
			serializer.serialize(bundle);
		} catch (IOException e)
		{
			// throw new APIException("serialization of bundle failed.");
		}

		sendBundle(out.toByteArray());

		// if (_receiver.getResponse().getCode() != 200)
		// {
		// throw new APIException("bundle rejected or put failed");
		// }

		// send the bundle away
		// if (query("bundle send") != 200) throw new
		// APIException("bundle send failed");
	}

	/**
	 * Send a bundle directly to the daemon.
	 * 
	 * @param destination
	 *            The destination of the bundle.
	 * @param lifetime
	 *            The lifetime of the bundle.
	 * @param data
	 *            The payload as byte-array.
	 * @throws APIException
	 *             If the transmission fails.
	 */
	public void send(EID destination, Integer lifetime, byte[] data)
	{
		send(destination, lifetime, data, Bundle.Priority.NORMAL);
	}

	/**
	 * Send a bundle directly to the daemon.
	 * 
	 * @param destination
	 *            The destination of the bundle.
	 * @param lifetime
	 *            The lifetime of the bundle.
	 * @param data
	 *            The payload as byte-array.
	 * @param priority
	 *            The priority of the bundle
	 * @throws APIException
	 *             If the transmission fails.
	 */
	public void send(EID destination, Integer lifetime, byte[] data, Bundle.Priority priority)
	{
		// wrapper to the send(Bundle) function
		Bundle bundle = new Bundle(destination, lifetime);
		bundle.appendBlock(new PayloadBlock(data));
		bundle.setPriority(priority);
		send(bundle);
	}

	/**
	 * Send a bundle directly to the daemon. The given stream is used as payload
	 * of the bundle.
	 * 
	 * @param destination
	 *            The destination of the bundle.
	 * @param lifetime
	 *            The lifetime of the bundle.
	 * @param stream
	 *            The stream containing the payload data.
	 * @param length
	 *            The length of the payload.
	 * @throws APIException
	 *             If the transmission fails.
	 */
	public void send(EID destination, Integer lifetime, InputStream stream, Long length)
	{
		send(destination, lifetime, stream, length, Bundle.Priority.NORMAL);
	}

	/**
	 * Send a bundle directly to the daemon. The given stream is used as payload
	 * of the bundle.
	 * 
	 * @param destination
	 *            The destination of the bundle.
	 * @param lifetime
	 *            The lifetime of the bundle.
	 * @param stream
	 *            The stream containing the payload data.
	 * @param length
	 *            The length of the payload.
	 * @param priority
	 *            The priority of the bundle
	 * @throws APIException
	 *             If the transmission fails.
	 */
	public void send(EID destination, Integer lifetime, InputStream stream, Long length, Bundle.Priority priority)
	{
		Bundle bundle = new Bundle(destination, lifetime);
		bundle.appendBlock(new PayloadBlock(new InputStreamBlockData(stream, length)));
		bundle.setPriority(priority);
		send(bundle);
	}
	
	public synchronized void initialize()
	{
		_daemon_online = true;
		_initialize_process.start();
		
		//TODO: We need to register!
		// this was done by _session.register(_registration);
//		invoke_registration_intent();
	}
	
	private Thread _initialize_process = new Thread() {
		@Override
		public void run() {
			try {
				while (!isInterrupted())
				{
					try {
						getSession();
						break;
					} catch (IOException e) {
						synchronized(ClientSession.this) {
							wait(5000);
						}
					}
				}
			} catch (InterruptedException e1) {	}
		}
	};
	
	public synchronized void terminate()
	{
		_daemon_online = false;
		_initialize_process.interrupt();
		
		if (_session != null)
		{
			_session.disconnect();
			_session = null;
		}
	}
	
	private synchronized APISession getSession() throws IOException
	{
		if (!_daemon_online) throw new IOException("daemon is offline");
		
		try {
			if (_session == null)
			{
				Log.d(TAG, "try to create an API session with the daemon");
				_session = new APISession(this);
				
//				APIConnection socket = this._manager.getAPIConnection();
//				if (socket == null) throw new IOException("daemon not running");
				
//				_session.connect(socket);
				_session.connect();
				_session.register(_registration);
				
				invoke_registration_intent();
			}
			
			if (_session.isConnected())
			{
				return _session;
			}
			else
			{
				_session = null;
				throw new IOException("not connected");
			}
		} catch (APIException e) {
			_session = null;
			throw new IOException("api error");
		}
	}
    
    private final DTNSession.Stub mBinder = new DTNSession.Stub()
    {
		public boolean query(DTNSessionCallback cb, BundleID id) throws RemoteException {
			try {
				APISession session = getSession();
				session.query(cb, id);
				return true;
			} catch (Exception e) {
				Log.e(TAG, "query failed", e);
				return false;
			}
		}

		public boolean delivered(BundleID id) throws RemoteException {
			try {
				APISession session = getSession();
				session.setDelivered(id);
				return true;
			} catch (Exception e) {
				Log.e(TAG, "delivered failed", e);
				return false;
			}
		}

		public boolean queryNext(DTNSessionCallback cb) throws RemoteException {
			try {
				APISession session = getSession();
				return session.query(cb);
			} catch (Exception e) {
				Log.e(TAG, "queryNext failed", e);
				return false;
			}
		}

		public boolean send(SingletonEndpoint destination,
				int lifetime, byte[] data) throws RemoteException {
			try {
				APISession session = getSession();
				return session.send(destination, lifetime, data);
			} catch (Exception e) {
				Log.e(TAG, "send failed", e);
				return false;
			}
		}

		public boolean sendGroup(GroupEndpoint destination,
				int lifetime, byte[] data) throws RemoteException {
			try {
				APISession session = getSession();
				return session.send(destination, lifetime, data);
			} catch (Exception e) {
				Log.e(TAG, "sendGroup failed", e);
				return false;
			}
		}

		public boolean sendFileDescriptor(SingletonEndpoint destination, int lifetime,
				ParcelFileDescriptor fd, long length) throws RemoteException {
			try {
				APISession session = getSession();
				return session.send(destination, lifetime, fd, length);
			} catch (Exception e) {
				Log.e(TAG, "sendFileDescriptor failed", e);
				return false;
			}
		}

		public boolean sendGroupFileDescriptor(GroupEndpoint destination, int lifetime,
				ParcelFileDescriptor fd, long length) throws RemoteException {
			try {
				APISession session = getSession();
				return session.send(destination, lifetime, fd, length);
			} catch (Exception e) {
				Log.e(TAG, "sendGroupFileDescriptor failed", e);
				return false;
			}
		}
    };
    
    public DTNSession getBinder()
    {
    	return mBinder;
    }
    
	public String getPackageName()
	{
		return _package_name;
	}
	
//	public synchronized void invoke_reconnect()
//	{
//		_session = null;
//	}
	
	public void invoke_receive_intent(BundleID id)
	{
		// forward the notification as intent
		// create a new intent
        Intent notify = new Intent(de.tubs.ibr.dtn.Intent.RECEIVE);
        notify.addCategory(_package_name);
        notify.putExtra("type", "bundle");
        notify.putExtra("data", id);

        // send notification intent
        context.sendBroadcast(notify);
        
        Log.d(TAG, "RECEIVE intent sent to " + _package_name);
	}
	
    private void invoke_registration_intent()
    {
		// send out registration intent to the application
		Intent broadcastIntent = new Intent(de.tubs.ibr.dtn.Intent.REGISTRATION);
		broadcastIntent.addCategory(_package_name);
		broadcastIntent.putExtra("key", _package_name);
		
		// send notification intent
		context.sendBroadcast(broadcastIntent);
		
		Log.d(TAG, "REGISTRATION intent sent to " + _package_name);
    }
}
