#ifndef __TRANSACTIONENGINE__
#define __TRANSACTIONENGINE__

#include <boost/unordered_set.hpp>
#include <boost/unordered_map.hpp>

#include "Ledger.h"
#include "SerializedTransaction.h"
#include "SerializedLedger.h"
#include "LedgerEntrySet.h"

// A TransactionEngine applies serialized transactions to a ledger
// It can also, verify signatures, verify fees, and give rejection reasons

enum TransactionEngineResult
{
	// Note: Numbers are currently unstable.  Use tokens.

	// tenCAN_NEVER_SUCCEED = <0

	// Malformed: Fee claimed
	tenGEN_IN_USE	= -300,
	tenBAD_ADD_AUTH,
	tenBAD_AMOUNT,
	tenBAD_CLAIM_ID,
	tenBAD_EXPIRATION,
	tenBAD_GEN_AUTH,
	tenBAD_ISSUER,
	tenBAD_OFFER,
	tenBAD_PATH,
	tenBAD_PATH_COUNT,
	tenBAD_PUBLISH,
	tenBAD_SET_ID,
	tenCREATEXNS,
	tenDST_IS_SRC,
	tenDST_NEEDED,
	tenEXPLICITXNS,
	tenREDUNDANT,
	tenRIPPLE_EMPTY,

	// Invalid: Ledger won't allow.
	tenCLAIMED		= -200,
	tenBAD_RIPPLE,
	tenCREATED,
	tenEXPIRED,
	tenMSG_SET,
	terALREADY,

	// Other
	tenFAILED		= -100,
	tenINSUF_FEE_P,
	tenINVALID,
	tenUNKNOWN,

	terSUCCESS		= 0,

	// terFAILED_BUT_COULD_SUCCEED = >0
	// Conflict with ledger database: Fee claimed
	// Might succeed if not conflict is not caused by transaction ordering.
	terBAD_AUTH,
	terBAD_AUTH_MASTER,
	terBAD_LEDGER,
	terBAD_RIPPLE,
	terBAD_SEQ,
	terCREATED,
	terDIR_FULL,
	terFUNDS_SPENT,
	terINSUF_FEE_B,
	terINSUF_FEE_T,
	terNODE_NOT_FOUND,
	terNODE_NOT_MENTIONED,
	terNODE_NO_ROOT,
	terNO_ACCOUNT,
	terNO_DST,
	terNO_LINE_NO_ZERO,
	terNO_PATH,
	terOFFER_NOT_FOUND,
	terOVER_LIMIT,
	terPAST_LEDGER,
	terPAST_SEQ,
	terPRE_SEQ,
	terSET_MISSING_DST,
	terUNCLAIMED,
	terUNFUNDED,

	// Might succeed in different order.
	// XXX claim fee and try to delete unfunded.
	terPATH_EMPTY,
	terPATH_PARTIAL,
};

bool transResultInfo(TransactionEngineResult terCode, std::string& strToken, std::string& strHuman);

enum TransactionEngineParams
{
	tepNONE          = 0,
	tepNO_CHECK_SIGN = 1,	// Signature already checked
	tepNO_CHECK_FEE  = 2,	// It was voted into a ledger anyway
	tepUPDATE_TOTAL  = 4,	// Update the total coins
	tepMETADATA      = 5,   // put metadata in tree, not transaction
};

typedef struct {
	uint16							uFlags;				// --> From path.

	uint160							uAccountID;			// --> Recieving/sending account.
	uint160							uCurrencyID;		// --> Accounts: receive and send, Offers: send.
														// --- For offer's next has currency out.
	uint160							uIssuerID;			// --> Currency's issuer

	// Computed by Reverse.
	STAmount						saRevRedeem;		// <-- Amount to redeem to next.
	STAmount						saRevIssue;			// <-- Amount to issue to next limited by credit and outstanding IOUs.
														//     Issue isn't used by offers.
	STAmount						saRevDeliver;		// <-- Amount to deliver to next regardless of fee.

	// Computed by forward.
	STAmount						saFwdRedeem;		// <-- Amount node will redeem to next.
	STAmount						saFwdIssue;			// <-- Amount node will issue to next.
														//	   Issue isn't used by offers.
	STAmount						saFwdDeliver;		// <-- Amount to deliver to next regardless of fee.
} paymentNode;

// Hold a path state under incremental application.
class PathState
{
protected:
	Ledger::pointer				mLedger;

	bool pushNode(int iType, uint160 uAccountID, uint160 uCurrencyID, uint160 uIssuerID);
	bool pushImply(uint160 uAccountID, uint160 uCurrencyID, uint160 uIssuerID);

public:
	typedef boost::shared_ptr<PathState> pointer;

	bool						bValid;
	std::vector<paymentNode>	vpnNodes;
	LedgerEntrySet				lesEntries;

	int							mIndex;
	uint64						uQuality;		// 0 = none.
	STAmount					saInReq;		// Max amount to spend by sender
	STAmount					saInAct;		// Amount spent by sender (calc output)
	STAmount					saOutReq;		// Amount to send (calc input)
	STAmount					saOutAct;		// Amount actually sent (calc output).

	PathState(
		Ledger::pointer			lpLedger,
		int						iIndex,
		const LedgerEntrySet&	lesSource,
		const STPath&			spSourcePath,
		uint160					uReceiverID,
		uint160					uSenderID,
		STAmount				saSend,
		STAmount				saSendMax,
		bool					bPartialPayment
		);

	Json::Value	getJson() const;

	static PathState::pointer createPathState(
		Ledger::pointer			lpLedger,
		int						iIndex,
		const LedgerEntrySet&	lesSource,
		const STPath&			spSourcePath,
		uint160					uReceiverID,
		uint160					uSenderID,
		STAmount				saSend,
		STAmount				saSendMax,
		bool					bPartialPayment
		)
	{
		PathState::pointer	pspNew = boost::make_shared<PathState>(lpLedger, iIndex, lesSource, spSourcePath, uReceiverID, uSenderID, saSend, saSendMax, bPartialPayment);

		return pspNew && pspNew->bValid ? pspNew : PathState::pointer();
	}

	static bool lessPriority(const PathState::pointer& lhs, const PathState::pointer& rhs);
};

// One instance per ledger.
// Only one transaction applied at a time.
class TransactionEngine
{
private:
	LedgerEntrySet						mNodes;

	TransactionEngineResult dirAdd(
		uint64&							uNodeDir,		// Node of entry.
		const uint256&					uRootIndex,
		const uint256&					uLedgerIndex);

	TransactionEngineResult dirDelete(
		bool							bKeepRoot,
		const uint64&					uNodeDir,		// Node item is mentioned in.
		const uint256&					uRootIndex,
		const uint256&					uLedgerIndex);	// Item being deleted

	bool dirFirst(const uint256& uRootIndex, SLE::pointer& sleNode, unsigned int& uDirEntry, uint256& uEntryIndex);
	bool dirNext(const uint256& uRootIndex, SLE::pointer& sleNode, unsigned int& uDirEntry, uint256& uEntryIndex);

	TransactionEngineResult	setAuthorized(const SerializedTransaction& txn, bool bMustSetGenerator);

	TransactionEngineResult takeOffers(
		bool				bPassive,
		const uint256&		uBookBase,
		const uint160&		uTakerAccountID,
		const SLE::pointer&	sleTakerAccount,
		const STAmount&		saTakerPays,
		const STAmount&		saTakerGets,
		STAmount&			saTakerPaid,
		STAmount&			saTakerGot);

protected:
	Ledger::pointer		mLedger;
	uint64				mLedgerParentCloseTime;

	uint160				mTxnAccountID;
	SLE::pointer		mTxnAccount;

	boost::unordered_set<uint256>	mUnfunded;	// Indexes that were found unfunded.

	SLE::pointer		entryCreate(LedgerEntryType letType, const uint256& uIndex);
	SLE::pointer		entryCache(LedgerEntryType letType, const uint256& uIndex);
	void				entryDelete(SLE::pointer sleEntry, bool unfunded = false);
	void				entryModify(SLE::pointer sleEntry);

	uint32				rippleTransferRate(const uint160& uIssuerID);
	STAmount			rippleBalance(const uint160& uToAccountID, const uint160& uFromAccountID, const uint160& uCurrencyID);
	STAmount			rippleLimit(const uint160& uToAccountID, const uint160& uFromAccountID, const uint160& uCurrencyID);
	uint32				rippleQualityIn(const uint160& uToAccountID, const uint160& uFromAccountID, const uint160& uCurrencyID);
	uint32				rippleQualityOut(const uint160& uToAccountID, const uint160& uFromAccountID, const uint160& uCurrencyID);

	STAmount			rippleHolds(const uint160& uAccountID, const uint160& uCurrencyID, const uint160& uIssuerID);
	STAmount			rippleTransferFee(const uint160& uSenderID, const uint160& uReceiverID, const uint160& uIssuerID, const STAmount& saAmount);
	void				rippleCredit(const uint160& uSenderID, const uint160& uReceiverID, const STAmount& saAmount, bool bCheckIssuer=true);
	STAmount			rippleSend(const uint160& uSenderID, const uint160& uReceiverID, const STAmount& saAmount);

	STAmount			accountHolds(const uint160& uAccountID, const uint160& uCurrencyID, const uint160& uIssuerID);
	STAmount			accountSend(const uint160& uSenderID, const uint160& uReceiverID, const STAmount& saAmount);
	STAmount			accountFunds(const uint160& uAccountID, const STAmount& saDefault);

	PathState::pointer	pathCreate(const STPath& spPath);
	void				pathNext(PathState::pointer pspCur, int iPaths);
	bool				calcNode(unsigned int uIndex, PathState::pointer pspCur, bool bMultiQuality);
	bool				calcNodeOfferRev(unsigned int uIndex, PathState::pointer pspCur, bool bMultiQuality);
	bool				calcNodeOfferFwd(unsigned int uIndex, PathState::pointer pspCur, bool bMultiQuality);
	bool				calcNodeAccountRev(unsigned int uIndex, PathState::pointer pspCur, bool bMultiQuality);
	bool				calcNodeAccountFwd(unsigned int uIndex, PathState::pointer pspCur, bool bMultiQuality);
	void				calcNodeRipple(const uint32 uQualityIn, const uint32 uQualityOut,
							const STAmount& saPrvReq, const STAmount& saCurReq,
							STAmount& saPrvAct, STAmount& saCurAct);

	void				txnWrite();

	TransactionEngineResult offerDelete(const SLE::pointer& sleOffer, const uint256& uOfferIndex, const uint160& uOwnerID);

	TransactionEngineResult doAccountSet(const SerializedTransaction& txn);
	TransactionEngineResult doClaim(const SerializedTransaction& txn);
	TransactionEngineResult doCreditSet(const SerializedTransaction& txn);
	TransactionEngineResult doDelete(const SerializedTransaction& txn);
	TransactionEngineResult doInvoice(const SerializedTransaction& txn);
	TransactionEngineResult doOfferCreate(const SerializedTransaction& txn);
	TransactionEngineResult doOfferCancel(const SerializedTransaction& txn);
	TransactionEngineResult doNicknameSet(const SerializedTransaction& txn);
	TransactionEngineResult doPasswordFund(const SerializedTransaction& txn);
	TransactionEngineResult doPasswordSet(const SerializedTransaction& txn);
	TransactionEngineResult doPayment(const SerializedTransaction& txn);
	TransactionEngineResult doStore(const SerializedTransaction& txn);
	TransactionEngineResult doTake(const SerializedTransaction& txn);
	TransactionEngineResult doWalletAdd(const SerializedTransaction& txn);

public:
	TransactionEngine() { ; }
	TransactionEngine(Ledger::pointer ledger) : mLedger(ledger) { ; }

	Ledger::pointer getLedger()						{ return mLedger; }
	void setLedger(Ledger::pointer ledger)			{ assert(ledger); mLedger = ledger; }

	TransactionEngineResult applyTransaction(const SerializedTransaction&, TransactionEngineParams);
};

inline TransactionEngineParams operator|(const TransactionEngineParams& l1, const TransactionEngineParams& l2)
{
	return static_cast<TransactionEngineParams>(static_cast<int>(l1) | static_cast<int>(l2));
}

inline TransactionEngineParams operator&(const TransactionEngineParams& l1, const TransactionEngineParams& l2)
{
	return static_cast<TransactionEngineParams>(static_cast<int>(l1) & static_cast<int>(l2));
}

#endif
// vim:ts=4
