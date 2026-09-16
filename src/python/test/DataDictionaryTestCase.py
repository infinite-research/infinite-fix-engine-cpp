import quickfix as fix
import quickfix40 as fix40
import os
import subprocess
import sys
import textwrap
import unittest

class TooHigh(fix.StringField):
    def __init__(self, data = None):
        if data == None:
            fix.StringField.__init__(self, 501)
        else:
            fix.StringField.__init__(self, 501, data)

class DataDictionaryTestCase(unittest.TestCase):

    def setUp(self):
        self.object = fix.DataDictionary()

    def assertBindingSubprocess(self, source):
        env = os.environ.copy()
        env["PYTHONPATH"] = os.pathsep.join(
            os.path.abspath(path or os.getcwd()) for path in sys.path
        )
        result = subprocess.run(
            [sys.executable, "-c", textwrap.dedent(source)],
            env=env,
            capture_output=True,
            text=True,
        )
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)

    def test_getGroup_missing_message_is_safe(self):
        self.assertBindingSubprocess("""
            import quickfix as fix

            dictionary = fix.DataDictionary()
            group = fix.DataDictionary()
            group.addMsgType("group")
            dictionary.addGroup("A", 100, 101, group)
            output = fix.DataDictionary()
            output.addMsgType("unchanged")
            for _ in range(10000):
                assert dictionary.getGroup("Z", 100, 7, output) is False
                assert output.isMsgType("unchanged")
        """)

    def test_getGroup_missing_tag_is_safe(self):
        self.assertBindingSubprocess("""
            import quickfix as fix

            dictionary = fix.DataDictionary()
            group = fix.DataDictionary()
            group.addMsgType("group")
            dictionary.addGroup("A", 100, 101, group)
            output = fix.DataDictionary()
            output.addMsgType("unchanged")
            for _ in range(10000):
                assert dictionary.getGroup("A", 999, 7, output) is False
                assert output.isMsgType("unchanged")
        """)

    def test_getGroup_wrong_output_type_raises(self):
        self.assertBindingSubprocess("""
            import quickfix as fix

            dictionary = fix.DataDictionary()
            group = fix.DataDictionary()
            dictionary.addGroup("A", 100, 101, group)
            try:
                dictionary.getGroup("A", 100, 0, object())
            except TypeError:
                pass
            else:
                raise AssertionError("wrong output type was accepted")
        """)

    def test_string_reference_placeholder_rejects_wrong_type(self):
        self.assertBindingSubprocess("""
            import quickfix as fix

            dictionary = fix.DataDictionary()
            dictionary.addFieldName(1, "Account")
            try:
                dictionary.getFieldName(1, object())
            except TypeError:
                pass
            else:
                raise AssertionError("wrong string reference placeholder was accepted")
        """)

    def test_int_reference_placeholder_rejects_wrong_type(self):
        self.assertBindingSubprocess("""
            import quickfix as fix

            dictionary = fix.DataDictionary()
            dictionary.addFieldName(1, "Account")
            try:
                dictionary.getFieldTag("Account", object())
            except TypeError:
                pass
            else:
                raise AssertionError("wrong int reference placeholder was accepted")
        """)

    def test_getGroup_recovers_after_failures(self):
        self.assertBindingSubprocess("""
            import quickfix as fix

            dictionary = fix.DataDictionary()
            group = fix.DataDictionary()
            group.addMsgType("group")
            dictionary.addGroup("A", 100, 101, group)
            output = fix.DataDictionary()
            assert dictionary.getGroup("Z", 100, 0, output) is False
            assert dictionary.getGroup("A", 999, 0, output) is False
            assert dictionary.getGroup("A", 100, 0, output) == {0: 101}
            assert output.isMsgType("group")
        """)

    def test_hasValidStructure_keeps_false_output_tag(self):
        self.assertBindingSubprocess("""
            import quickfix as fix

            message = fix.Message()
            message.setString(
                "8=FIX.4.2\\x019=0\\x0135=A\\x0110=000\\x01108=30\\x0110=000\\x01",
                False,
            )
            assert message.hasValidStructure(0) == {0: 108}
        """)

    def test_addMsgType(self):
        self.assertTrue( self.object.isMsgType("A") == 0 )
        self.object.addMsgType("A")
        self.assertTrue( self.object.isMsgType("A") )

    def test_addMsgField(self):
        self.assertTrue( self.object.isMsgField( "A", 10 ) == 0 )
        self.assertTrue( self.object.isMsgField( "Z", 50 ) == 0 )
        self.object.addMsgField( "A", 10 )
        self.object.addMsgField( "Z", 50 )
        self.assertTrue( self.object.isMsgField( "A", 10 ) )
        self.assertTrue( self.object.isMsgField( "Z", 50 ) )
        self.assertTrue( self.object.isMsgField( "A", 50 ) == 0 )
        self.assertTrue( self.object.isMsgField( "Z", 10 ) == 0 )

    def test_addHeaderField(self):
        self.assertTrue( self.object.isHeaderField( 56 ) == 0 )
        self.assertTrue( self.object.isHeaderField( 49 ) == 0 )
        self.object.addHeaderField( 56, True )
        self.object.addHeaderField( 49, True )
        self.assertTrue( self.object.isHeaderField( 56 ) )
        self.assertTrue( self.object.isHeaderField( 49 ) )

    def test_addTrailerField(self):
        self.assertTrue( self.object.isTrailerField( 10 ) == 0 )
        self.object.addTrailerField( 10, True )
        self.assertTrue( self.object.isTrailerField( 10 ) )

    def test_addRequiredField(self):
        self.assertTrue( self.object.isRequiredField( "A", 10 ) == 0 )
        self.assertTrue( self.object.isRequiredField( "Z", 50 ) == 0 )
        self.object.addRequiredField( "A", 10 )
        self.object.addRequiredField( "Z", 50 )
        self.assertTrue( self.object.isRequiredField( "A", 10 ) )
        self.assertTrue( self.object.isRequiredField( "Z", 50 ) )
        self.assertTrue( self.object.isRequiredField( "A", 50 ) == 0 )
        self.assertTrue( self.object.isRequiredField( "Z", 10 ) == 0 )

    def test_addFieldValue(self):
        self.assertTrue( self.object.isFieldValue( 12, "f" ) == 0 )
        self.assertTrue( self.object.isFieldValue( 12, "g" ) == 0 )
        self.assertTrue( self.object.isFieldValue( 15, "1" ) == 0 )
        self.assertTrue( self.object.isFieldValue( 18, "2" ) == 0 )
        self.assertTrue( self.object.isFieldValue( 167, "FUT" ) == 0 )

        self.object.addFieldValue( 12, "f" )
        self.object.addFieldValue( 12, "g" )
        self.object.addFieldValue( 15, "1" )
        self.object.addFieldValue( 18, "2" )
        self.object.addFieldValue( 167, "FUT" )

        self.assertTrue( self.object.isFieldValue( 12, "f" ) )
        self.assertTrue( self.object.isFieldValue( 12, "g" ) )
        self.assertTrue( self.object.isFieldValue( 15, "1" ) )
        self.assertTrue( self.object.isFieldValue( 18, "2" ) )
        self.assertTrue( self.object.isFieldValue( 167, "FUT" ) )

    def test_addGroup(self):
        self.object.setVersion( "FIX.4.2" )

        group1 = fix.DataDictionary()
        group1.addMsgType( "1" )
        group2 = fix.DataDictionary()
        group2.addMsgType( "2" )
        group3 = fix.DataDictionary()
        group3.addMsgType( "3" )

        self.object.addGroup( "A", 100, 101, group1 )
        self.object.addGroup( "A", 200, 201, group2 )
        self.object.addGroup( "A", 300, 301, group3 )

        d = fix.DataDictionary()
        delim = 0

        result = self.object.getGroup( "A", 100, delim, d )
        self.assertEqual( 101, result[0] )
        self.assertTrue( d.isMsgType( "1" ) )

        result = self.object.getGroup( "A", 200, delim, d )
        self.assertEqual( 201, result[0] )
        self.assertTrue( d.isMsgType( "2" ) )

        result = self.object.getGroup( "A", 300, delim, d )
        self.assertEqual( 301, result[0] )
        self.assertTrue( d.isMsgType( "3" ) )

    def test_addFieldName(self):
        self.object.setVersion( "FIX.4.2" )

        self.object.addFieldName( 1, "Account" )
        self.object.addFieldName( 11, "ClOrdID" )
        self.object.addFieldName( 8, "BeginString" )

        self.assertEqual( "Account", self.object.getFieldName(1, "")[0] )
        self.assertEqual( 1, self.object.getFieldTag("Account", 0)[0] )
        self.assertEqual( "ClOrdID", self.object.getFieldName(11, "")[0] )
        self.assertEqual( 11, self.object.getFieldTag("ClOrdID", 0)[0] )
        self.assertEqual( "BeginString", self.object.getFieldName(8, "")[0] )
        self.assertEqual( 8, self.object.getFieldTag("BeginString", 0)[0] )

    def test_addValueName(self):
        self.object.setVersion( "FIX.4.2" )

        self.object.addValueName( 12, "0", "VALUE_12_0" )
        self.object.addValueName( 12, "B", "VALUE_12_B" )
        self.object.addValueName( 23, "BOO", "VALUE_23_BOO" )

        self.assertEqual( "VALUE_12_0", self.object.getValueName(12, "0", "")[0] )
        self.assertEqual( "VALUE_12_B", self.object.getValueName(12, "B", "")[0] )
        self.assertEqual( "VALUE_23_BOO", self.object.getValueName(23, "BOO", "")[0] )

    def test_checkValidTagNumber(self):
        self.object.setVersion( fix.BeginString_FIX40 )
        self.object.addField( fix.BeginString().getTag() )
        self.object.addField( fix.BodyLength().getTag() )
        self.object.addField( fix.MsgType().getTag() )
        self.object.addField( fix.CheckSum().getTag() )
        self.object.addField( fix.TestReqID().getTag() )
        self.object.addMsgType( fix.MsgType_TestRequest )
        self.object.addMsgField( fix.MsgType_TestRequest, fix.TestReqID().getTag() )

        testReqID = fix.TestReqID( "1" )
        message = fix40.TestRequest()
        message.setField( testReqID )
        message.setField( TooHigh( "value" ) )

        try:
            self.object.validate( message )
            self.assertTrue( 0 )
        except fix.InvalidTagNumber as e:
            self.assertTrue( 1 )

        self.object.addField( 501 )
        self.object.addMsgField( fix.MsgType_TestRequest, 501 )
        try:
            self.object.validate( message )
            self.assertTrue( 1 )
        except fix.RequiredTagMissing as e:
            self.assertTrue( 0 )

        message.setField( 5000, "value" )
        try:
            self.object.validate( message )
            self.assertTrue( 0 )
        except fix.InvalidTagNumber as e:
            self.assertTrue( 1 )

        self.object.checkUserDefinedFields( False )
        try:
            self.object.validate( message )
            self.assertTrue( 1 )
        except fix.RequiredTagMissing as e:
            self.assertTrue( 0 )

    def test_checkHasValue(self):
        message = fix40.TestRequest()
        message.setField( fix.TestReqID() )

        try:
            self.object.validate( message )
            self.assertTrue( 0 )
        except fix.NoTagValue as e:
            self.assertTrue( 1 )

    def test_checkIsInMessage(self):
        self.object.setVersion( fix.BeginString_FIX40 )
        self.object.addField( fix.BeginString().getTag() )
        self.object.addField( fix.BodyLength().getTag() )
        self.object.addField( fix.MsgType().getTag() )
        self.object.addField( fix.CheckSum().getTag() )
        self.object.addField( fix.TestReqID().getTag() )
        self.object.addField( fix.Symbol().getTag() )
        self.object.addMsgType( fix.MsgType_TestRequest )
        self.object.addMsgField( fix.MsgType_TestRequest, fix.TestReqID().getTag() )

        testReqID = fix.TestReqID( "1" )

        message = fix40.TestRequest()
        message.setField( testReqID )
        try:
            self.object.validate( message )
            self.assertTrue( 1 )
        except fix.RequiredTagMissing as e:
            self.assertTrue( 0 )

        message.setField( fix.Symbol("MSFT") )
        try:
            self.object.validate( message )
            self.assertTrue( 0 )
        except fix.TagNotDefinedForMessage as e:
            self.assertTrue( 1 )

    def test_checkHasRequired(self):
        self.object.setVersion( fix.BeginString_FIX40 )
        self.object.addField( fix.BeginString().getTag() )
        self.object.addField( fix.BodyLength().getTag() )
        self.object.addField( fix.MsgType().getTag() )
        self.object.addField( fix.SenderCompID().getTag() )
        self.object.addField( fix.TargetCompID().getTag() )
        self.object.addHeaderField( fix.SenderCompID().getTag(), True )
        self.object.addHeaderField( fix.TargetCompID().getTag(), False )
        self.object.addField( fix.CheckSum().getTag() )
        self.object.addField( fix.TestReqID().getTag() )
        self.object.addMsgType( fix.MsgType_TestRequest )
        self.object.addMsgField( fix.MsgType_TestRequest, fix.TestReqID().getTag() )
        self.object.addRequiredField( fix.MsgType_TestRequest, fix.TestReqID().getTag() )

        message = fix40.TestRequest()
        try:
            self.object.validate( message )
            self.assertTrue( 0 )
        except fix.RequiredTagMissing as e:
            self.assertTrue( 1 )

        message.getHeader().setField( fix.SenderCompID("SENDER") )
        try:
            self.object.validate( message )
            self.assertTrue( 0 )
        except fix.RequiredTagMissing as e:
            self.assertTrue( 1 )

        message.setField( fix.TestReqID("1") )
        try:
            self.object.validate( message )
            self.assertTrue( 1 )
        except fix.RequiredTagMissing as e:
            self.assertTrue( 0 )

        message.getHeader().removeField( fix.SenderCompID().getTag() )
        message.setField( fix.SenderCompID("SENDER") )
        try:
            self.object.validate( message )
            self.assertTrue( 0 )
        except fix.RequiredTagMissing as e:
            self.assertTrue( 1 )

if __name__ == '__main__':
    unittest.main()
