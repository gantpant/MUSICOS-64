
console.log("MUSICOS64 website initialized");

document.addEventListener("DOMContentLoaded", () => {
    console.log("Welcome to MUSICOS64!");

    // Автоматичний рік у футері
    const footer = document.querySelector("footer p");

    if (footer) {
        footer.textContent = `© ${new Date().getFullYear()} MUSICOS64`;
    }
});
