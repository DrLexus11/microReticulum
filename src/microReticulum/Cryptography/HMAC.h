/*
 * Copyright (c) 2023 Chad Attermann
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 */

#pragma once

#include "../Bytes.h"

#include <Hash.h>
#include <SHA256.h>
#include <SHA512.h>
#include <stdexcept>
#include <memory>
#include <new>
#include <cassert>

namespace RNS { namespace Cryptography {

    class HMAC {

    public:
		enum Digest {
			DIGEST_NONE,
			DIGEST_SHA256,
			DIGEST_SHA512,
		};

		using Ptr = std::shared_ptr<HMAC>;

	public:
		/*
		Create a new HMAC object.
		key: bytes or buffer, key for the keyed hash object.
		msg: bytes or buffer, Initial input for the hash or None.
		digest: The underlying hash algorithm to use
		*/
		HMAC(const Bytes& key, const Bytes& msg = {Bytes::NONE}, Digest digest = DIGEST_SHA256) {

			if (digest == DIGEST_NONE) {
				throw std::invalid_argument("Cannot derive key from empty input material");
			}

			// Constructed in place. This used to be `new SHA256()`, which put a
			// heap allocation on the hot path of every packet encrypt and
			// decrypt -- the most frequent allocation in the whole stack. On a
			// board with a small heap it is both the main source of
			// fragmentation and, when it finally fails, fatal: operator new
			// throws bad_alloc, nothing catches it, and the node aborts and
			// reboots rather than dropping one packet.
			//
			// Traced on an ESP32 with 216 KB of heap: abort() from
			// __cxa_allocate_exception, under HMAC::HMAC, under Token::encrypt.
			switch (digest) {
			case DIGEST_SHA256:
				_hash = new (_storage) SHA256();
				break;
			case DIGEST_SHA512:
				_hash = new (_storage) SHA512();
				break;
			default:
				throw std::invalid_argument("Unknown ior unsuppored digest");
			}

			_key = key;
			_hash->resetHMAC(key.data(), key.size());

			if (msg) {
				update(msg);
			}
		}

		~HMAC() {
			// Hash declares a virtual destructor, so this dispatches to the
			// concrete one whichever digest was placed in the storage.
			if (_hash != nullptr) _hash->~Hash();
		}

		// The hash lives inside this object, so a copy would leave two objects
		// pointing at one storage. Nothing copies an HMAC; say so rather than
		// leave it to be discovered.
		HMAC(const HMAC&) = delete;
		HMAC& operator=(const HMAC&) = delete;

		/*
		Feed data from msg into this hashing object.
		*/
		void update(const Bytes& msg) {
			assert(_hash);
			_hash->update(msg.data(), msg.size());
		}

		/*
		Return the hash value of this hashing object.
		This returns the hmac value as bytes.  The object is
		not altered in any way by this function; you can continue
		updating the object after calling this function.
		*/
		Bytes digest() {
			assert(_hash);
			Bytes result;
			_hash->finalizeHMAC(_key.data(), _key.size(), result.writable(_hash->hashSize()), _hash->hashSize());
			return result;
		}

		/*
		Create a new hashing object and return it.
		key: bytes or buffer, The starting key for the hash.
		msg: bytes or buffer, Initial input for the hash, or None.
		digest: The underlying hash algorithm to use.
		You can now feed arbitrary bytes into the object using its update()
		method, and can ask for the hash value at any time by calling its digest()
		method.
		*/
		static inline Ptr generate(const Bytes& key, const Bytes& msg = {Bytes::NONE}, Digest digest = DIGEST_SHA256) {
			return Ptr(new HMAC(key, msg, digest));
		}

		/*
		The digest of key and msg, without allocating.

		generate() hands back a shared_ptr, so every caller that wants only the
		digest pays for a heap-allocated HMAC it discards on the next line.
		Token does exactly that on every encrypt and every decrypt. This is the
		same computation on the stack.
		*/
		static inline Bytes compute(const Bytes& key, const Bytes& msg,
		                            Digest digest = DIGEST_SHA256) {
			HMAC hmac(key, msg, digest);
			return hmac.digest();
		}

	private:
		Bytes _key;
		// Sized for the largest digest supported, so one buffer serves both and
		// the choice stays a runtime one.
		alignas(alignof(SHA512)) uint8_t _storage[
			sizeof(SHA512) > sizeof(SHA256) ? sizeof(SHA512) : sizeof(SHA256)];
		Hash* _hash = nullptr;

	};

	/*
	Fast inline implementation of HMAC.
	key: bytes or buffer, The key for the keyed hash object.
	msg: bytes or buffer, Input message.
	digest: The underlying hash algorithm to use.
	*/
	inline const Bytes digest(const Bytes& key, const Bytes& msg, HMAC::Digest digest = HMAC::DIGEST_SHA256) {
		// No second update(): the constructor already feeds msg in, so the old
		// body hashed the message twice and produced a digest that would not
		// verify against any other implementation. Dormant -- nothing calls
		// this -- but wrong, and now it is the same computation as compute().
		return HMAC::compute(key, msg, digest);
	}

} }
