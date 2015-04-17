/*
 * Copyright (c) 2010 .SE (The Internet Infrastructure Foundation)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*****************************************************************************
 BotanECDSAPrivateKey.cpp

 Botan ECDSA private key class
 *****************************************************************************/

#include "config.h"
#ifdef WITH_ECC
#include "log.h"
#include "BotanECDSAPrivateKey.h"
#include "BotanCryptoFactory.h"
#include "BotanRNG.h"
#include "BotanUtil.h"
#include <string.h>
#include <botan/pkcs8.h>
#include <botan/ber_dec.h>
#include <botan/der_enc.h>
#include <botan/asn1_oid.h>
#include <botan/oids.h>

// Constructors
BotanECDSAPrivateKey::BotanECDSAPrivateKey()
{
	eckey = NULL;
}

BotanECDSAPrivateKey::BotanECDSAPrivateKey(const Botan::ECDSA_PrivateKey* inECKEY)
{
	eckey = NULL;

	setFromBotan(inECKEY);
}

// Destructor
BotanECDSAPrivateKey::~BotanECDSAPrivateKey()
{
	delete eckey;
}

// The type
/*static*/ const char* BotanECDSAPrivateKey::type = "Botan ECDSA Private Key";

// Get the base point order length
unsigned long BotanECDSAPrivateKey::getOrderLength() const
{
	try
	{
		Botan::EC_Group group = BotanUtil::byteString2ECGroup(ec);
		return group.get_order().bytes();
	}
	catch (...)
	{
		ERROR_MSG("Can't get EC group for order length");

		return 0;
	}
}

// Set from Botan representation
void BotanECDSAPrivateKey::setFromBotan(const Botan::ECDSA_PrivateKey* inECKEY)
{
	ByteString inEC = BotanUtil::ecGroup2ByteString(inECKEY->domain());
	setEC(inEC);
	ByteString inD = BotanUtil::bigInt2ByteString(inECKEY->private_value());
	setD(inD);
}

// Check if the key is of the given type
bool BotanECDSAPrivateKey::isOfType(const char* inType)
{
	return !strcmp(type, inType);
}

// Setters for the ECDSA private key components
void BotanECDSAPrivateKey::setD(const ByteString& inD)
{
	ECPrivateKey::setD(inD);

	if (eckey)
	{
		delete eckey;
		eckey = NULL;
	}
}

// Setters for the ECDSA public key components
void BotanECDSAPrivateKey::setEC(const ByteString& inEC)
{
	ECPrivateKey::setEC(inEC);

	if (eckey)
	{
		delete eckey;
		eckey = NULL;
	}
}

// Encode into PKCS#8 DER
ByteString BotanECDSAPrivateKey::PKCS8Encode()
{
	ByteString der;
	createBotanKey();
	if (eckey == NULL) return der;
	// Force EC_DOMPAR_ENC_OID
	const size_t PKCS8_VERSION = 0;
#if BOTAN_VERSION_MINOR == 11
	const std::vector<Botan::byte> parameters = eckey->domain().DER_encode(Botan::EC_DOMPAR_ENC_OID);
	const Botan::AlgorithmIdentifier alg_id(eckey->get_oid(), parameters);
	const Botan::secure_vector<Botan::byte> ber =
		Botan::DER_Encoder()
		.start_cons(Botan::SEQUENCE)
		    .encode(PKCS8_VERSION)
		    .encode(alg_id)
		    .encode(eckey->pkcs8_private_key(), Botan::OCTET_STRING)
		.end_cons()
	    .get_contents();
#else
	const Botan::MemoryVector<Botan::byte> parameters = eckey->domain().DER_encode(Botan::EC_DOMPAR_ENC_OID);
	const Botan::AlgorithmIdentifier alg_id(eckey->get_oid(), parameters);
	const Botan::SecureVector<Botan::byte> ber =
		Botan::DER_Encoder()
		.start_cons(Botan::SEQUENCE)
		    .encode(PKCS8_VERSION)
		    .encode(alg_id)
		    .encode(eckey->pkcs8_private_key(), Botan::OCTET_STRING)
		.end_cons()
	    .get_contents();
#endif
	der.resize(ber.size());
	memcpy(&der[0], &ber[0], ber.size());
	return der;
}

// Decode from PKCS#8 BER
bool BotanECDSAPrivateKey::PKCS8Decode(const ByteString& ber)
{
	Botan::DataSource_Memory source(ber.const_byte_str(), ber.size());
	if (source.end_of_data()) return false;
#if BOTAN_VERSION_MINOR == 11
	Botan::secure_vector<Botan::byte> keydata;
#else
	Botan::SecureVector<Botan::byte> keydata;
#endif
	Botan::AlgorithmIdentifier alg_id;
	Botan::ECDSA_PrivateKey* key = NULL;
	try
	{
		Botan::BER_Decoder(source)
		.start_cons(Botan::SEQUENCE)
			.decode_and_check<size_t>(0, "Unknown PKCS #8 version number")
			.decode(alg_id)
			.decode(keydata, Botan::OCTET_STRING)
			.discard_remaining()
		.end_cons();
		if (keydata.empty())
			throw Botan::Decoding_Error("PKCS #8 private key decoding failed");
		if (Botan::OIDS::lookup(alg_id.oid).compare("ECDSA"))
		{
			ERROR_MSG("Decoded private key not ECDSA");

			return false;
		}
		key = new Botan::ECDSA_PrivateKey(alg_id, keydata);
		if (key == NULL) return false;

		setFromBotan(key);

		delete key;
	}
	catch (std::exception& e)
	{
		ERROR_MSG("Decode failed on %s", e.what());

		return false;
	}

	return true;
}

// Retrieve the Botan representation of the key
Botan::ECDSA_PrivateKey* BotanECDSAPrivateKey::getBotanKey()
{
	if (!eckey)
	{
		createBotanKey();
	}

	return eckey;
}

// Create the Botan representation of the key
void BotanECDSAPrivateKey::createBotanKey()
{
	if (ec.size() != 0 &&
	    d.size() != 0)
	{
		if (eckey)
		{
			delete eckey;
			eckey = NULL;
		}

		try
		{
			BotanRNG* rng = (BotanRNG*)BotanCryptoFactory::i()->getRNG();
			Botan::EC_Group group = BotanUtil::byteString2ECGroup(ec);
			eckey = new Botan::ECDSA_PrivateKey(*rng->getRNG(),
							group,
							BotanUtil::byteString2bigInt(d));
		}
		catch (...)
		{
			ERROR_MSG("Could not create the Botan public key");
		}
	}
}
#endif
