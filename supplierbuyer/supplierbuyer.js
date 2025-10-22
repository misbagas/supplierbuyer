function setAdminLinks() {
    // Change Login button to Admin and point to adminpage.html
    var loginBtn = document.getElementById('loginBtn');
    if (loginBtn) {
        loginBtn.textContent = 'Admin';
        loginBtn.href = '/admin/adminpage.html'; // <-- FIXED
    }

    // Change Join Now button to Admin and point to adminpage.html
    var joinBtn = document.querySelector('a.btn[href="/auth/joinpage.html"]');
    if (joinBtn) {
        joinBtn.textContent = 'Admin';
        joinBtn.href = '/admin/adminpage.html'; // <-- FIXED
    }
}

// Call setAdminLinks() if the user is logged in
if (localStorage.getItem('isLoggedIn') === 'true') {
    setAdminLinks();
}

// When login/register succeeds in your code:
/// localStorage.setItem('isLoggedIn', 'true');
/// setAdminLinks();

