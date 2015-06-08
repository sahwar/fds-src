package com.formationds.rest.v08.model;

import com.formationds.client.v08.model.SizeUnit;
import org.junit.Assert;
import org.junit.Test;

import java.util.EnumSet;

/**
 * SizeUnit Tester.
 *
 * @author <Authors name>
 * @version 1.0
 * @since <pre>May 29, 2015</pre>
 */
public class SizeUnitTest {

    @Test
    public void printConversions() {
        System.out.println( SizeUnit.B.toMega( 1048576 ) );
        System.out.printf( "1 B = %s B\n", SizeUnit.B.toBytes( 1 ) );
        System.out.printf( "1 KB = %s B\n", SizeUnit.KB.toBytes( 1 ) );
        System.out.printf( "1 MB = %s B\n", SizeUnit.MB.toBytes( 1 ) );
        System.out.printf( "1 GB = %s B\n", SizeUnit.GB.toBytes( 1 ) );
        System.out.printf( "1 TB = %s B\n", SizeUnit.TB.toBytes( 1 ) );
        System.out.printf( "1 PB = %s B\n", SizeUnit.PB.toBytes( 1 ) );
        System.out.printf( "1 XB = %s B\n", SizeUnit.EB.toBytes( 1 ) );
        System.out.printf( "1 ZB = %s B\n", SizeUnit.ZB.toBytes( 1 ) );
        System.out.printf( "1 YB = %s B\n", SizeUnit.YB.toBytes( 1 ) );

        EnumSet<SizeUnit> bsus = EnumSet.allOf( SizeUnit.class );
        for ( SizeUnit bsu : bsus ) {
            System.out.printf( "1 %s = %s %s\n", bsu.symbol(), bsu.toBytes( 1 ), SizeUnit.B.symbol() );
            System.out.printf( "1 %s = %s %s\n", bsu.symbol(), bsu.toKilo( 1 ), SizeUnit.KB.symbol() );
            System.out.printf( "1 %s = %s %s\n", bsu.symbol(), bsu.toMega( 1 ), SizeUnit.MB.symbol() );
            System.out.printf( "1 %s = %s %s\n", bsu.symbol(), bsu.toGiga( 1 ), SizeUnit.GB.symbol() );
            System.out.printf( "1 %s = %s %s\n", bsu.symbol(), bsu.toTera( 1 ), SizeUnit.TB.symbol() );
            System.out.printf( "1 %s = %s %s\n", bsu.symbol(), bsu.toPeta( 1 ), SizeUnit.PB.symbol() );
            System.out.printf( "1 %s = %s %s\n", bsu.symbol(), bsu.toExa( 1 ), SizeUnit.EB.symbol() );
            System.out.printf( "1 %s = %s %s\n", bsu.symbol(), bsu.toZetta( 1 ), SizeUnit.ZB.symbol() );
            System.out.printf( "1 %s = %s %s\n", bsu.symbol(), bsu.toYotta( 1 ), SizeUnit.YB.symbol() );
//
//            System.out.printf( "1 %s = %s %s\n", bsu.symbol(), bsu.toBibi( 1 ), SizeUnit.B.symbol() );
//            System.out.printf( "1 %s = %s %s\n", bsu.symbol(), bsu.toKibi( 1 ), SizeUnit.KiB.symbol() );
//            System.out.printf( "1 %s = %s %s\n", bsu.symbol(), bsu.toMebi( 1 ), SizeUnit.MiB.symbol() );
//            System.out.printf( "1 %s = %s %s\n", bsu.symbol(), bsu.toGibi( 1 ), SizeUnit.GiB.symbol() );
//            System.out.printf( "1 %s = %s %s\n", bsu.symbol(), bsu.toTebi( 1 ), SizeUnit.TiB.symbol() );
//            System.out.printf( "1 %s = %s %s\n", bsu.symbol(), bsu.toPebi( 1 ), SizeUnit.PiB.symbol() );
//            System.out.printf( "1 %s = %s %s\n", bsu.symbol(), bsu.toExbi( 1 ), SizeUnit.EiB.symbol() );
//            System.out.printf( "1 %s = %s %s\n", bsu.symbol(), bsu.toZebi( 1 ), SizeUnit.ZiB.symbol() );
//            System.out.printf( "1 %s = %s %s\n", bsu.symbol(), bsu.toYibi( 1 ), SizeUnit.YiB.symbol() );

        }
    }

    /**
     * Method: lookup(String s)
     */
    @Test
    public void testLookup() throws Exception {
        Assert.assertNotNull( SizeUnit.lookup( "B" ) );
        Assert.assertNotNull( SizeUnit.lookup( "byte" ) );
        Assert.assertNotNull( SizeUnit.lookup( "Byte" ) );
        Assert.assertNotNull( SizeUnit.lookup( "B" ) );
        Assert.assertNotNull( SizeUnit.lookup( "b" ) );

    }

    /**
     * Method: base()
     */
    @Test
    public void testBase() throws Exception {
        //TODO: Test goes here... 
    }

    /**
     * Method: exp()
     */
    @Test
    public void testExp() throws Exception {
        //TODO: Test goes here... 
    }

    /**
     * Method: symbol()
     */
    @Test
    public void testSymbol() throws Exception {
        //TODO: Test goes here... 
    }

    /**
     * Method: toBytes(long v)
     */
    @Test
    public void testToBytesV() throws Exception {
        //TODO: Test goes here... 
    }

    /**
     * Method: toKilo(long v)
     */
    @Test
    public void testToKiloV() throws Exception {
        //TODO: Test goes here... 
    }

    /**
     * Method: toMega(long v)
     */
    @Test
    public void testToMegaV() throws Exception {
        //TODO: Test goes here... 
    }

    /**
     * Method: toGiga(long v)
     */
    @Test
    public void testToGigaV() throws Exception {
        //TODO: Test goes here... 
    }

    /**
     * Method: toTera(long v)
     */
    @Test
    public void testToTeraV() throws Exception {
        //TODO: Test goes here... 
    }

    /**
     * Method: toPeta(long v)
     */
    @Test
    public void testToPetaV() throws Exception {
        //TODO: Test goes here... 
    }

    /**
     * Method: toExa(long v)
     */
    @Test
    public void testToExaV() throws Exception {
        //TODO: Test goes here... 
    }

    /**
     * Method: toZetta(long v)
     */
    @Test
    public void testToZettaV() throws Exception {
        //TODO: Test goes here... 
    }

    /**
     * Method: toYotta(long v)
     */
    @Test
    public void testToYottaV() throws Exception {
        //TODO: Test goes here... 
    }

    /**
     * Method: toBibi(long v)
     */
    @Test
    public void testToBibiV() throws Exception {
        //TODO: Test goes here... 
    }

    /**
     * Method: toKibi(long v)
     */
    @Test
    public void testToKibiV() throws Exception {
        //TODO: Test goes here... 
    }

    /**
     * Method: toMibi(long v)
     */
    @Test
    public void testToMibiV() throws Exception {
        //TODO: Test goes here... 
    }

    /**
     * Method: toGibi(long v)
     */
    @Test
    public void testToGibiV() throws Exception {
        //TODO: Test goes here... 
    }

    /**
     * Method: toTibi(long v)
     */
    @Test
    public void testToTibiV() throws Exception {
        //TODO: Test goes here... 
    }

    /**
     * Method: toPibi(long v)
     */
    @Test
    public void testToPibiV() throws Exception {
        //TODO: Test goes here... 
    }

    /**
     * Method: toExbi(long v)
     */
    @Test
    public void testToExbiV() throws Exception {
        //TODO: Test goes here... 
    }

    /**
     * Method: toZibi(long v)
     */
    @Test
    public void testToZibiV() throws Exception {
        //TODO: Test goes here... 
    }

    /**
     * Method: toYibi(long v)
     */
    @Test
    public void testToYibiV() throws Exception {
        //TODO: Test goes here... 
    }

    /**
     * Method: convertTo(SizeUnit to, long value)
     */
    @Test
    public void testConvertToForToValue() throws Exception {
        //TODO: Test goes here... 
    }
} 