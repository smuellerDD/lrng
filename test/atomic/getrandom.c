// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/*
* Copyright (C) 2018, Stephan Mueller <smueller@chronox.de>
*
* THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
* OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, ALL OF
* WHICH ARE HEREBY DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE
* LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
* OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
* LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
* USE OF THIS SOFTWARE, EVEN IF NOT ADVISED OF THE POSSIBILITY OF SUCH
* DAMAGE.
*/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/random.h>
#include <linux/spinlock.h>


static DEFINE_SPINLOCK(lock);

static int __init getrandom_init(void)
{
	unsigned long flags;
	unsigned int i;
	u8 buf[16];

	for (i = 0; i < 100000; i++) {
		spin_lock_irqsave(&lock, flags);
		get_random_bytes(buf, sizeof(buf));
		spin_unlock_irqrestore(&lock, flags);
	}

	return -EAGAIN;
}

static void __exit getrandom_exit(void)
{
	return;
}

module_init(getrandom_init);
module_exit(getrandom_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Stephan Mueller <smueller@chronox.de>");
MODULE_DESCRIPTION("Kernel module getrandom");

