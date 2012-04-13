/* $Id: templateengine.py 2241 2006-05-22 07:58:58Z fischer $ */

///
/// @file        MulticastSocketTest.hh
/// @brief       CPPUnit-Tests for class MulticastSocket
/// @author      Author Name (email@mail.address)
/// @date        Created at 2010-11-01
/// 
/// @version     $Revision: 2241 $
/// @note        Last modification: $Date: 2006-05-22 09:58:58 +0200 (Mon, 22 May 2006) $
///              by $Author: fischer $
///

 
#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>

#ifndef MULTICASTSOCKETTEST_HH
#define MULTICASTSOCKETTEST_HH
class MulticastSocketTest : public CppUnit::TestFixture {
	private:
	public:
		/*=== BEGIN tests for class 'MulticastSocket' ===*/
		void testBind();
		void testSetInterface();
		void testJoinGroup();
		void testLeaveGroup();
		void testIsMulticast();
		/*=== END   tests for class 'MulticastSocket' ===*/

		void setUp();
		void tearDown();


		CPPUNIT_TEST_SUITE(MulticastSocketTest);
			CPPUNIT_TEST(testBind);
			CPPUNIT_TEST(testSetInterface);
			CPPUNIT_TEST(testJoinGroup);
			CPPUNIT_TEST(testLeaveGroup);
			CPPUNIT_TEST(testIsMulticast);
		CPPUNIT_TEST_SUITE_END();
};
#endif /* MULTICASTSOCKETTEST_HH */